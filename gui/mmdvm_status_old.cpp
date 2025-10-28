#include <chrono>
#include <csignal>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <cstring>
#include <thread>
#include <vector>
#include <cctype>

#include <mariadb/mysql.h> // libmariadb-dev

namespace fs = std::filesystem;
static bool running = true;

/* --------- Debug Logger --------- */
std::ofstream dbg;
void dbg_open() {
    try {
        fs::create_directories("/var/log");
        dbg.open("/var/log/mmdvm-status.log", std::ios::app);
    } catch (...) {}
}
template<typename... T> void dlog(T&&... t) {
    ((std::cout << std::forward<T>(t)), ...);
    std::cout << std::endl;
    if (dbg.is_open()) {
        ((dbg << std::forward<T>(t)), ...);
        dbg << std::endl;
        dbg.flush();
    }
}

/* --------- Status --------- */
struct Status {
    std::string mode = "Idle";
    std::string callsign;
    bool active = false;
    std::optional<double> last_ber;
    std::optional<double> last_duration;
    std::string updated_at_iso;
    bool from_network = false;                    // RF vs. NET (nur Info)
    std::optional<int> ysf_dgid;

    // Startzeit der aktuellen TX (zur Heuristik-Dauerberechnung)
    std::optional<std::chrono::steady_clock::time_point> tx_started;
};

std::string now_iso8601() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    std::tm tm{}; localtime_r(&t, &tm);
    char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm);
    return buf;
}

// --- Callsign-Check: mind. 3 Zeichen, mind. eine Ziffer ---
static inline bool callsign_valid(const std::string& cs) {
    if (cs.size() < 3) return false;
    for (unsigned char ch : cs) {
        if (std::isdigit(ch)) return true;
    }
    return false;
}

/* --------- DB Wrapper (MariaDB) --------- */
struct Db {
    MYSQL* conn = nullptr;
    MYSQL_STMT* st_upsert_status = nullptr;
    MYSQL_STMT* st_insert_lastheard = nullptr;
    MYSQL_STMT* st_upsert_reflector_dstar = nullptr;
    MYSQL_STMT* st_upsert_reflector_fusion = nullptr;
    MYSQL_STMT* st_upsert_reflector_dmr = nullptr;

    std::string host = "localhost";
    std::string user = "mmdvm";
    std::string pass = "";
    std::string name = "mmdvmdb";
    unsigned int port = 0;
    std::string unix_socket = "/run/mysqld/mysqld.sock";

    bool connect() {
        if (conn) { mysql_close(conn); conn = nullptr; }
        conn = mysql_init(nullptr);
        if (!conn) { dlog("[DB  ] mysql_init failed"); return false; }

        unsigned int reconnect = 1;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

        // Explizit das Protokoll auf UNIX-Socket setzen (zusätzlich zu unix_socket-Arg)
        unsigned int proto = MYSQL_PROTOCOL_SOCKET;
        mysql_options(conn, MYSQL_OPT_PROTOCOL, &proto);

        // WICHTIG: Passwort = "", Port = 0, unix_socket = Pfad
        if (!mysql_real_connect(
                conn,
                nullptr,                 // host: nullptr erzwingt lokalen Connect
                user.c_str(),            // "mmdvm"
                "",                      // KEIN Passwort
                name.c_str(),            // DB-Name
                0,                       // Port 0
                unix_socket.c_str(),     // Pfad zum Socket
                0)) {                    // Flags
            dlog("[DB  ] connect failed: ", mysql_error(conn));
            mysql_close(conn); conn = nullptr;
            return false;
        }

        dlog("[DB  ] connected via unix_socket=", unix_socket, " db=", name);

        // Tabellen anlegen (idempotent)
        const char* q1 =
            "CREATE TABLE IF NOT EXISTS lastheard ("
            " id INT AUTO_INCREMENT PRIMARY KEY,"
            " callsign VARCHAR(20),"
            " mode VARCHAR(20),"
            " dgid INT NULL,"
            " source ENUM('RF','NET') NULL,"
            " duration FLOAT,"
            " ber FLOAT,"
            " ts DATETIME DEFAULT CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
        if (mysql_query(conn, q1) != 0) dlog("[DB  ] create lastheard failed: ", mysql_error(conn));

        const char* q2 =
            "CREATE TABLE IF NOT EXISTS status ("
            " id TINYINT PRIMARY KEY,"
            " mode VARCHAR(20),"
            " callsign VARCHAR(20),"
            " dgid INT NULL,"
            " source ENUM('RF','NET') NULL,"
            " active BOOL,"
            " ber FLOAT,"
            " duration FLOAT,"
            " updated_at DATETIME"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
        if (mysql_query(conn, q2) != 0) dlog("[DB  ] create status failed: ", mysql_error(conn));

        const char* q3 =
            "CREATE TABLE IF NOT EXISTS reflector ("
            " id TINYINT PRIMARY KEY,"
            " dstar  VARCHAR(64) NULL,"
            " dmr    VARCHAR(64) NULL,"
            " fusion VARCHAR(64) NULL,"
            " updated_at DATETIME"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
        if (mysql_query(conn, q3) != 0) dlog("[DB  ] create reflector failed: ", mysql_error(conn));

        // Seed-Zeile
        mysql_query(conn, "INSERT IGNORE INTO reflector (id,dstar,dmr,fusion,updated_at) "
                        "VALUES (1,NULL,NULL,NULL,NOW());");

        // Statements vorbereiten
        prepare_statements();
        // Statuszeile seed'en falls leer
        mysql_query(conn, "INSERT IGNORE INTO status (id,mode,callsign,dgid,source,active,ber,duration,updated_at) "
                          "VALUES (1,'Idle','',NULL,'RF',0,NULL,NULL,NOW());");

        return true;
    }

    void prepare_statements() {
        destroy_statements();

        // UPSERT status
        const char* ps1 =
            "INSERT INTO status (id, mode, callsign, dgid, source, active, ber, duration, updated_at) "
            "VALUES (1, ?, ?, ?, ?, ?, ?, ?, NOW()) "
            "ON DUPLICATE KEY UPDATE "
            " mode=VALUES(mode), callsign=VALUES(callsign), dgid=VALUES(dgid), source=VALUES(source), "
            " active=VALUES(active), ber=VALUES(ber), duration=VALUES(duration), updated_at=NOW();";

        st_upsert_status = mysql_stmt_init(conn);
        if (!st_upsert_status || mysql_stmt_prepare(st_upsert_status, ps1, (unsigned long)strlen(ps1)) != 0) {
            dlog("[DB  ] prepare upsert status failed: ", mysql_error(conn));
            destroy_statements();
        }

        // INSERT lastheard (dgid/source/duration/ber können NULL sein)
        const char* ps2 =
            "INSERT INTO lastheard (callsign, mode, dgid, source, duration, ber, ts) "
            "VALUES (?, ?, ?, ?, ?, ?, NOW());";

        st_insert_lastheard = mysql_stmt_init(conn);
        if (!st_insert_lastheard || mysql_stmt_prepare(st_insert_lastheard, ps2, (unsigned long)strlen(ps2)) != 0) {
            dlog("[DB  ] prepare insert lastheard failed: ", mysql_error(conn));
            destroy_statements();
        }

        // --- UPSERT reflector.dstar ---
        const char* ps3 =
            "INSERT INTO reflector (id, dstar, updated_at) "
            "VALUES (1, ?, NOW()) "
            "ON DUPLICATE KEY UPDATE "
            " dstar=VALUES(dstar), updated_at=NOW();";

        st_upsert_reflector_dstar = mysql_stmt_init(conn);
        if (!st_upsert_reflector_dstar ||
            mysql_stmt_prepare(st_upsert_reflector_dstar, ps3, (unsigned long)strlen(ps3)) != 0) {
            dlog("[DB  ] prepare upsert reflector.dstar failed: ", mysql_error(conn));
            destroy_statements();
        }

        // --- UPSERT reflector.fusion ---
        const char* ps4 =
            "INSERT INTO reflector (id, fusion, updated_at) "
            "VALUES (1, ?, NOW()) "
            "ON DUPLICATE KEY UPDATE "
            " fusion=VALUES(fusion), updated_at=NOW();";

        st_upsert_reflector_fusion = mysql_stmt_init(conn);
        if (!st_upsert_reflector_fusion ||
            mysql_stmt_prepare(st_upsert_reflector_fusion, ps4, (unsigned long)strlen(ps4)) != 0) {
            dlog("[DB  ] prepare upsert reflector.fusion failed: ", mysql_error(conn));
            destroy_statements();
        }

        // --- UPSERT reflector.dmr ---
        const char* ps5 =
            "INSERT INTO reflector (id, dmr, updated_at) "
            "VALUES (1, ?, NOW()) "
            "ON DUPLICATE KEY UPDATE "
            " dmr=VALUES(dmr), updated_at=NOW();";

        st_upsert_reflector_dmr = mysql_stmt_init(conn);
        if (!st_upsert_reflector_dmr ||
            mysql_stmt_prepare(st_upsert_reflector_dmr, ps5, (unsigned long)strlen(ps5)) != 0) {
            dlog("[DB  ] prepare upsert reflector.dmr failed: ", mysql_error(conn));
            destroy_statements();
        }
    }

    void destroy_statements() {
        if (st_upsert_status) { mysql_stmt_close(st_upsert_status); st_upsert_status = nullptr; }
        if (st_insert_lastheard) { mysql_stmt_close(st_insert_lastheard); st_insert_lastheard = nullptr; }
        if (st_upsert_reflector_dstar) { mysql_stmt_close(st_upsert_reflector_dstar); st_upsert_reflector_dstar =
            nullptr; }
        if (st_upsert_reflector_fusion) { mysql_stmt_close(st_upsert_reflector_fusion); st_upsert_reflector_fusion = nullptr; }
        if (st_upsert_reflector_dmr) { mysql_stmt_close(st_upsert_reflector_dmr); st_upsert_reflector_dmr = nullptr; }
    }

    bool ensure_conn() {
        if (!conn) return connect();
        if (mysql_ping(conn) != 0) {
            dlog("[DB  ] ping failed: ", mysql_error(conn), " -> reconnect");
            return connect();
        }
        return true;
    }

    bool upsert_reflector_dmr(const std::optional<std::string>& refl) {
        if (!ensure_conn() || !st_upsert_reflector_dmr) return false;

        MYSQL_BIND b[1]{}; memset(b, 0, sizeof(b));
        unsigned long len = 0;
        my_bool is_null = refl.has_value() ? 0 : 1;
        const char* ptr = nullptr;
        std::string tmp;

        if (refl.has_value()) {
            tmp = *refl;
            ptr = tmp.c_str();
            len = (unsigned long)tmp.size();
        }
        b[0].buffer_type = MYSQL_TYPE_STRING;
        b[0].buffer = (void*)ptr;
        b[0].length = &len;
        b[0].is_null = &is_null;

        if (mysql_stmt_bind_param(st_upsert_reflector_dmr, b) != 0) {
            dlog("[DB  ] bind reflector.dmr failed: ", mysql_stmt_error(st_upsert_reflector_dmr));
            return false;
        }
        if (mysql_stmt_execute(st_upsert_reflector_dmr) != 0) {
            dlog("[DB  ] exec reflector.dmr failed: ", mysql_stmt_error(st_upsert_reflector_dmr));
            return false;
        }
        return true;
    }

    bool upsert_reflector_dstar(const std::optional<std::string>& refl) {
        if (!ensure_conn() || !st_upsert_reflector_dstar) return false;

        MYSQL_BIND b[1]{}; memset(b, 0, sizeof(b));

        unsigned long len = 0;
        my_bool is_null = refl.has_value() ? 0 : 1;
        const char* ptr = nullptr;

        std::string tmp;
        if (refl.has_value()) {
            tmp = *refl;
            ptr = tmp.c_str();
            len = (unsigned long)tmp.size();
        }

        b[0].buffer_type = MYSQL_TYPE_STRING;
        b[0].buffer = (void*)ptr;    // darf NULL sein, wenn is_null=1
        b[0].length = &len;
        b[0].is_null = &is_null;

        if (mysql_stmt_bind_param(st_upsert_reflector_dstar, b) != 0) {
            dlog("[DB  ] bind reflector.dstar failed: ", mysql_stmt_error(st_upsert_reflector_dstar));
            return false;
        }
        if (mysql_stmt_execute(st_upsert_reflector_dstar) != 0) {
            dlog("[DB  ] exec reflector.dstar failed: ", mysql_stmt_error(st_upsert_reflector_dstar));
            return false;
        }
        return true;
    }

    bool upsert_reflector_fusion(const std::optional<std::string>& refl) {
        if (!ensure_conn() || !st_upsert_reflector_fusion) return false;

        MYSQL_BIND b[1]{}; memset(b, 0, sizeof(b));
        unsigned long len = 0;
        my_bool is_null = refl.has_value() ? 0 : 1;
        const char* ptr = nullptr;

        std::string tmp;
        if (refl.has_value()) {
            tmp = *refl;
            ptr = tmp.c_str();
            len = (unsigned long)tmp.size();
        }

        b[0].buffer_type = MYSQL_TYPE_STRING;
        b[0].buffer = (void*)ptr;     // darf NULL sein, wenn is_null=1
        b[0].length = &len;
        b[0].is_null = &is_null;

        if (mysql_stmt_bind_param(st_upsert_reflector_fusion, b) != 0) {
            dlog("[DB  ] bind reflector.fusion failed: ", mysql_stmt_error(st_upsert_reflector_fusion));
            return false;
        }
        if (mysql_stmt_execute(st_upsert_reflector_fusion) != 0) {
            dlog("[DB  ] exec reflector.fusion failed: ", mysql_stmt_error(st_upsert_reflector_fusion));
            return false;
        }
        return true;
    }


    bool upsert_status(const Status& s) {
        if (!ensure_conn() || !st_upsert_status) return false;

        MYSQL_BIND b[8]{}; // mode, callsign, dgid, source, active, ber, duration
        memset(b, 0, sizeof(b));

        // mode
        b[0].buffer_type = MYSQL_TYPE_STRING;
        b[0].buffer = (void*)s.mode.c_str();
        unsigned long mode_len = (unsigned long)s.mode.size();
        b[0].length = &mode_len;

        // callsign (nur wenn plausibel, sonst leer speichern)
        std::string cs_to_store = callsign_valid(s.callsign) ? s.callsign : std::string();
        b[1].buffer_type = MYSQL_TYPE_STRING;
        b[1].buffer = (void*)cs_to_store.c_str();
        unsigned long cs_len = (unsigned long)cs_to_store.size();
        b[1].length = &cs_len;

        // dgid (nullable)
        int dgid_val = s.ysf_dgid.value_or(0);
        my_bool dgid_is_null = s.ysf_dgid.has_value() ? 0 : 1;
        b[2].buffer_type = MYSQL_TYPE_LONG;
        b[2].buffer = &dgid_val;
        b[2].is_null = &dgid_is_null;

        // source (ENUM als STRING)
        std::string src = s.from_network ? "NET" : "RF";
        b[3].buffer_type = MYSQL_TYPE_STRING;
        b[3].buffer = (void*)src.c_str();
        unsigned long src_len = (unsigned long)src.size();
        b[3].length = &src_len;

        // active
        signed char active_val = s.active ? 1 : 0;
        b[4].buffer_type = MYSQL_TYPE_TINY;
        b[4].buffer = &active_val;

        // ber (nullable)
        double ber_val = s.last_ber.value_or(0.0);
        my_bool ber_is_null = s.last_ber.has_value() ? 0 : 1;
        b[5].buffer_type = MYSQL_TYPE_DOUBLE;
        b[5].buffer = &ber_val;
        b[5].is_null = &ber_is_null;

        // duration (nullable)
        double dur_val = s.last_duration.value_or(0.0);
        my_bool dur_is_null = s.last_duration.has_value() ? 0 : 1;
        b[6].buffer_type = MYSQL_TYPE_DOUBLE;
        b[6].buffer = &dur_val;
        b[6].is_null = &dur_is_null;


        if (mysql_stmt_bind_param(st_upsert_status, b) != 0) {
            dlog("[DB  ] bind upsert status failed: ", mysql_stmt_error(st_upsert_status));
            return false;
        }
        if (mysql_stmt_execute(st_upsert_status) != 0) {
            dlog("[DB  ] exec upsert status failed: ", mysql_stmt_error(st_upsert_status));
            return false;
        }
        return true;
    }

    bool insert_lastheard(const std::string& callsign,
                          const std::string& mode,
                          std::optional<int> dgid,
                          const std::string& source,  // "RF" oder "NET"
                          std::optional<double> duration,
                          std::optional<double> ber) {
        if (!ensure_conn() || !st_insert_lastheard) return false;

        MYSQL_BIND b[6]{}; memset(b, 0, sizeof(b));

        // callsign
        b[0].buffer_type = MYSQL_TYPE_STRING;
        b[0].buffer = (void*)callsign.c_str();
        unsigned long cs_len = (unsigned long)callsign.size();
        b[0].length = &cs_len;

        // mode
        b[1].buffer_type = MYSQL_TYPE_STRING;
        b[1].buffer = (void*)mode.c_str();
        unsigned long mode_len = (unsigned long)mode.size();
        b[1].length = &mode_len;

        // dgid (nullable)
        int dgid_val = dgid.value_or(0);
        my_bool dgid_is_null = dgid.has_value() ? 0 : 1;
        b[2].buffer_type = MYSQL_TYPE_LONG;
        b[2].buffer = &dgid_val;
        b[2].is_null = &dgid_is_null;

        // source (ENUM als STRING)
        b[3].buffer_type = MYSQL_TYPE_STRING;
        b[3].buffer = (void*)source.c_str();
        unsigned long src_len = (unsigned long)source.size();
        b[3].length = &src_len;

        // duration (nullable)
        double dur_val = duration.value_or(0.0);
        my_bool dur_is_null = duration.has_value() ? 0 : 1;
        b[4].buffer_type = MYSQL_TYPE_DOUBLE;
        b[4].buffer = &dur_val;
        b[4].is_null = &dur_is_null;

        // ber (nullable)
        double ber_val = ber.value_or(0.0);
        my_bool ber_is_null = ber.has_value() ? 0 : 1;
        b[5].buffer_type = MYSQL_TYPE_DOUBLE;
        b[5].buffer = &ber_val;
        b[5].is_null = &ber_is_null;

        if (mysql_stmt_bind_param(st_insert_lastheard, b) != 0) {
            dlog("[DB  ] bind lastheard failed: ", mysql_stmt_error(st_insert_lastheard));
            return false;
        }
        if (mysql_stmt_execute(st_insert_lastheard) != 0) {
            dlog("[DB  ] exec lastheard failed: ", mysql_stmt_error(st_insert_lastheard));
            return false;
        }
        return true;
    }

    ~Db() {
        destroy_statements();
        if (conn) { mysql_close(conn); conn = nullptr; }
    }
};

/* --------- Logfile Handling --------- */

fs::path file_last_write_latest(const fs::path& dir) {
    fs::path best; std::time_t best_time = 0;
    std::regex pat(R"(MMDVM-\d{4}-\d{2}-\d{2}\.log)");
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        auto name = e.path().filename().string();
        if (!std::regex_match(name, pat)) continue;
        auto ftime = fs::last_write_time(e.path());
        // portable conversion to system_clock::time_point
        auto sctp = std::chrono::system_clock::now() + (ftime - fs::file_time_type::clock::now());
        std::time_t t = std::chrono::system_clock::to_time_t(sctp);
        if (t >= best_time) { best_time = t; best = e.path(); }
    }
    return best;
}

fs::path file_last_write_latest_with_pattern(const fs::path& dir, const std::regex& pat) {
    fs::path best; std::time_t best_time = 0;
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        auto name = e.path().filename().string();
        if (!std::regex_match(name, pat)) continue;
        auto ftime = fs::last_write_time(e.path());
        auto sctp = std::chrono::system_clock::now() + (ftime - fs::file_time_type::clock::now());
        std::time_t t = std::chrono::system_clock::to_time_t(sctp);
        if (t >= best_time) { best_time = t; best = e.path(); }
    }
    return best;
}

void on_signal(int){ running = false; }

/* --------- Heuristik: TX sauber abschließen --------- */

void finish_tx_if_running(Status& st, Db& db, const char* reason) {
    if (!st.active) return;

    std::optional<double> dur;
    if (st.tx_started) {
        using namespace std::chrono;
        auto secs = duration_cast<duration<double>>(steady_clock::now() - *st.tx_started).count();
        if (secs < 0) secs = 0;
        dur = secs;
        st.last_duration = secs;
    } else {
        st.last_duration.reset();
    }

    // Bei heuristischem Ende keine BER
    st.last_ber.reset();

    if (!st.callsign.empty()) {
        std::string src = st.from_network ? "NET" : "RF";
        std::optional<int> dgid_to_store = (st.mode == "System Fusion") ? st.ysf_dgid : std::nullopt;

        if (callsign_valid(st.callsign)) {
            if (!db.insert_lastheard(st.callsign, st.mode, dgid_to_store, src, st.last_duration, st.last_ber)) {
                dlog("[DB  ] insert lastheard (heuristic) failed");
            } else {
                dlog("[DMR ] TX finished (", reason, "): callsign=", st.callsign,
                    ", dur=", (st.last_duration ? std::to_string(*st.last_duration) : std::string("NULL")), "s");
            }
        } else {
            dlog("[SKIP] invalid callsign for lastheard: '", st.callsign, "'");
        }
    }


    st.active = false;
    st.tx_started.reset();
    st.updated_at_iso = now_iso8601();
    if (!db.upsert_status(st)) dlog("[DB  ] upsert status failed");
}

/* --------- Parser --------- */
static inline std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

// YSFGateway (Fusion) – Reflector-Status
static std::regex rx_ysf_linked(R"(Linked to\s+(.+)$)");
static std::regex rx_ysf_closing(R"(Closing YSF network connection)");

// DMRGateway – Master-Login := Reflector-Set
static const std::regex rx_dmrgw_login(
    R"(^\s*[A-Z]:\s+\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d+\s+([^,]+),\s*Logged into (?:the\s+)?master successfully)"
);

// Optional: Unlink/Clear (falls du später solche Zeilen hast)
static const std::regex rx_dmrgw_close(
    R"(^.*?,\s*(?:Closing connection to master|Logged out|Master is down).*$)");

void handle_line_dmrgw(const std::string& line, Db& db) {
    std::smatch m;
    if (std::regex_search(line, m, rx_dmrgw_login)) {
        auto name = trim(m[1].str()); // z.B. "BM_2621_Germany"
        if (!name.empty()) {
            dlog("[DMR ] Reflector set: ", name);
            if (!db.upsert_reflector_dmr(name)) {
                dlog("[DB  ] upsert reflector.dmr failed");
            }
        }
        return;
    }
    if (std::regex_search(line, m, rx_dmrgw_close)) {
        dlog("[DMR ] Reflector cleared");
        if (!db.upsert_reflector_dmr(std::nullopt)) {
            dlog("[DB  ] clear reflector.dmr failed");
        }
        return;
    }
    // sonst ignorieren
}

void handle_line_ysfgw(const std::string& line, Db& db) {
    std::smatch m;
    if (std::regex_search(line, m, rx_ysf_linked)) {
        auto refl = trim(m[1].str());
        if (!refl.empty()) {
            dlog("[YSF ] Reflector set: ", refl);
            if (!db.upsert_reflector_fusion(refl)) {
                dlog("[DB  ] upsert reflector.fusion failed");
            }
        }
        return;
    }
    if (std::regex_search(line, m, rx_ysf_closing)) {
        dlog("[YSF ] Reflector cleared");
        if (!db.upsert_reflector_fusion(std::nullopt)) {
            dlog("[DB  ] clear reflector.fusion failed");
        }
        return;
    }
    // sonst ignorieren
}

void handle_line_and_db(const std::string& line, Status& st, Db& db) {
    static std::regex rx_mode(R"(Mode set to\s+([A-Za-z0-9 \-]+)$)");
    static std::regex rx_ysf_hdr(R"(YSF,\s+received RF (?:header|late entry) from\s+(\S+))");
    static std::regex rx_dstar_hdr(R"(D-Star,\s+received RF (?:header|late entry) from\s+(\S+))");
    static std::regex rx_dmr_hdr(R"(DMR,\s+received RF (?:header|late entry) from\s+(\S+))");

    // NEW: YSF Network (Start & EOT)
    //  M: ... YSF, received network data from HA8WM      to DG-ID 32 at HA8WM
    //  M: ... YSF, received network end of transmission from HA8WM      to DG-ID 32, 3.3 seconds, 0% packet loss, BER: 0.0%
    static std::regex rx_ysf_net_data(
        R"(YSF,\s+received network data from\s+(\S+)\s+to DG-ID\s+(\d+))");
    static std::regex rx_ysf_net_eot(
        R"(YSF,\s+received network end of transmission from\s+(\S+)\s+to DG-ID\s+(\d+),\s+([0-9]+(?:\.[0-9]+)?)\s+seconds,.*BER:\s+([0-9]+(?:\.[0-9]+)?)%)");

    // DMR Startzeile im neuen Format
    static std::regex rx_dmr_downlink_activate(R"(Downlink Activate received from\s+(\S+))");

    //static std::regex rx_eot(R"(received RF end of transmission.*?([0-9]+\.[0-9]+|[0-9]+)\s+seconds,\s+BER:\s+([0-9]+\.[0-9]+|[0-9]+)\%)");
    static std::regex rx_eot(
        R"((?:received RF end of transmission|transmission lost).*?([0-9]+(?:\.[0-9]+)?)\s+seconds,\s+BER:\s+([0-9]+(?:\.[0-9]+)?)%)"
    );
    // YSF Network: Watchdog (EOT-Äquivalent)
    static std::regex rx_ysf_net_watchdog(
        R"(YSF,\s+network watchdog has expired,\s+([0-9]+(?:\.[0-9]+)?)\s+seconds,.*BER:\s+([0-9]+(?:\.[0-9]+)?)%)");


    // D-Star Reflector-Status (präzise!)
    // Slow data NUR wenn explizit "Verlinkt zu ..." / "Linked to ..."
    static std::regex rx_dstar_link_slowdata(
        R"(D-Star,\s+network slow data text\s*=\s*\"(?:Verlinkt zu|Linked to)\s+([^\"]+)\")");

    // Link-Status-Zeile (setzt)
    static std::regex rx_dstar_link_status_set(
        R"(D-Star link status set to\s*\"(?:Verlinkt zu|Linked to)\s+([^\"]+)\")");

    // und zusätzlich
    static std::regex rx_dstar_network_header_via(
        R"(D-Star,\s+received network header from\s+\S+\s+to\s+\S+\s+via\s+([A-Z0-9\-]+(?:\s+[A-Z])?))");

    // Unlink (löscht) – beide Varianten abdecken
    static std::regex rx_dstar_unlinked(
        R"(D-Star (?:link status set to|,?\s*network slow data text\s*=\s*)\"(?:Nicht verbunden|Not linked|Unlinked)[^\"]*\")");


    std::smatch m;
    bool changed = false;

    // Mode-Wechsel
    if (std::regex_search(line, m, rx_mode)) {
        std::string new_mode = m[1].str();
        // Heuristischer TX-Abschluss, wenn DMR aktiv war und jetzt "Idle" kommt
        if (st.active && st.mode == "DMR" && new_mode == "Idle") {
            finish_tx_if_running(st, db, "mode->Idle");
        }
        st.mode = new_mode;
        st.updated_at_iso = now_iso8601();
        if (st.mode == "Idle") st.active = false;
        dlog("[PARSE] mode = ", st.mode);
        changed = true;
    }

    // --- YSF NETWORK: Start ---
    if (std::regex_search(line, m, rx_ysf_net_data)) {
        st.callsign = m[1].str();
        try { st.ysf_dgid = std::stoi(m[2].str()); } catch (...) { st.ysf_dgid.reset(); }
        st.active = true;
        st.updated_at_iso = now_iso8601();
        st.mode = "System Fusion";
        st.from_network = true;
        st.tx_started = std::chrono::steady_clock::now();
        st.last_duration.reset();
        st.last_ber.reset();
        dlog("[PARSE] YSF NET data, cs=", st.callsign, " DGID=", (st.ysf_dgid? std::to_string(*st.ysf_dgid):""));
        changed = true;
    }

    // --- YSF NETWORK: End of transmission ---
    if (std::regex_search(line, m, rx_ysf_net_eot)) {
        st.active = false;
        st.last_duration = std::stod(m[3].str());
        st.last_ber = std::stod(m[4].str());
        st.updated_at_iso = now_iso8601();
        st.tx_started.reset();
        dlog("[PARSE] YSF NET EOT: cs=", m[1].str(), " dur=", *st.last_duration, "s BER=", *st.last_ber, "%");
        if (!st.callsign.empty())
            db.insert_lastheard(st.callsign, st.mode, st.ysf_dgid, "NET", st.last_duration, st.last_ber);
         changed = true;
    }

    // --- YSF NETWORK: Watchdog -> wie EOT behandeln ---
    if (std::regex_search(line, m, rx_ysf_net_watchdog)) {
        st.active = false;
        st.last_duration = std::stod(m[1].str());
        st.last_ber = std::stod(m[2].str());
        st.updated_at_iso = now_iso8601();
        st.tx_started.reset();

        dlog("[PARSE] YSF NET watchdog EOT: dur=", *st.last_duration, "s BER=", *st.last_ber, "%");

        // Lastheard → DB (nur wenn Callsign plausibel), Quelle: NET, DG-ID nur bei YSF
        if (!st.callsign.empty()) {
            if (callsign_valid(st.callsign)) {
                std::optional<int> dgid_to_store = st.ysf_dgid; // YSF: ja
                if (!db.insert_lastheard(st.callsign, st.mode, dgid_to_store, "NET", st.last_duration, st.last_ber)) {
                    dlog("[DB  ] insert lastheard (YSF watchdog) failed");
                }
            } else {
                dlog("[SKIP] invalid callsign for lastheard (YSF watchdog): '", st.callsign, "'");
            }
        }
        changed = true;
    }

    // YSF/D-Star/DMR (klassische Header)
    if (std::regex_search(line, m, rx_ysf_hdr)) {
        st.callsign = m[1].str();
        st.active = true;
        st.updated_at_iso = now_iso8601();
        st.mode = "System Fusion";
        st.tx_started = std::chrono::steady_clock::now();
        st.from_network = false;
        st.ysf_dgid.reset();
        dlog("[PARSE] YSF header, callsign=", st.callsign);
        changed = true;
    } else if (std::regex_search(line, m, rx_dstar_hdr)) {
        st.callsign = m[1].str();
        st.active = true;
        st.updated_at_iso = now_iso8601();
        st.mode = "D-Star";
        st.tx_started = std::chrono::steady_clock::now();

        st.from_network = false;
        st.ysf_dgid.reset();

        dlog("[PARSE] D-Star header, callsign=", st.callsign);
        changed = true;
    } else if (std::regex_search(line, m, rx_dmr_hdr)) {
        st.callsign = m[1].str();
        st.active = true;
        st.updated_at_iso = now_iso8601();
        st.mode = "DMR";
        st.tx_started = std::chrono::steady_clock::now();

        // Sinnvoll: RF-Quelle und DG-ID löschen (nur YSF nutzt DG-ID)
        st.from_network = false;
        st.ysf_dgid.reset();

        dlog("[PARSE] DMR header, callsign=", st.callsign);
        changed = true;
    }


    // DMR Downlink Activate → Start ODER Ende-vorher + Start-neu
    if (std::regex_search(line, m, rx_dmr_downlink_activate)) {
        std::string new_cs = m[1].str();

        // Wenn gerade eine DMR-TX läuft und ein anderes Rufzeichen aktiviert: vorherige TX beenden
        if (st.active && st.mode == "DMR" && !st.callsign.empty() && st.callsign != new_cs) {
            finish_tx_if_running(st, db, "new DMR activate with different callsign");
        }

        // Neue/aktuelle Aktivierung setzen
        st.callsign = new_cs;
        st.mode = "DMR";
        st.active = true;
        st.updated_at_iso = now_iso8601();
        st.tx_started = std::chrono::steady_clock::now();
        dlog("[PARSE] DMR Downlink Activate, callsign=", st.callsign);
        changed = true;
    }

    // Klassischer EOT → enthält Dauer & BER (falls vorhanden)
    if (std::regex_search(line, m, rx_eot)) {
        st.active = false;
        st.last_duration = std::stod(m[1].str());
        st.last_ber = std::stod(m[2].str());
        st.updated_at_iso = now_iso8601();
        st.tx_started.reset();
        dlog("[PARSE] EOT: dur=", *st.last_duration, "s, BER=", *st.last_ber, "%");

        // Lastheard → DB (mit echten Werten)
        if (!st.callsign.empty()) {
            std::string src = st.from_network ? "NET" : "RF";
            std::optional<int> dgid_to_store = (st.mode == "System Fusion") ? st.ysf_dgid : std::nullopt;
            if (!db.insert_lastheard(st.callsign, st.mode, dgid_to_store, src, st.last_duration, st.last_ber)) {
                dlog("[DB  ] insert lastheard failed");
            }
        }
        changed = true;
    }

    // --- D-Star Reflector-Status (Slow Data & Link-Statuszeilen) ---
    if (std::regex_search(line, m, rx_dstar_network_header_via)) {
        auto refl = trim(m[1].str());
        if (!refl.empty()) {
            dlog("[PARSE] D-Star reflector observed via: ", refl);
            db.upsert_reflector_dstar(refl); // optionales Re-Set
        }
    }

    {
        std::smatch mref;

        // SET via Slow-Data "Verlinkt zu ..." / "Linked to ..."
        if (std::regex_search(line, mref, rx_dstar_link_slowdata)) {
            auto refl = trim(mref[1].str());
            if (!refl.empty()) {
                dlog("[PARSE] D-Star reflector set (slowdata): ", refl);
                if (!db.upsert_reflector_dstar(refl)) {
                    dlog("[DB  ] upsert reflector.dstar failed");
                }
            }
            // WICHTIG: nicht weiter prüfen, return/continue nicht nötig – einfach durchfallen
        }
        // SET via "D-Star link status set to "Verlinkt zu ... / Linked to ..."
        else if (std::regex_search(line, mref, rx_dstar_link_status_set)) {
            auto refl = trim(mref[1].str());
            if (!refl.empty()) {
                dlog("[PARSE] D-Star reflector set (status): ", refl);
                if (!db.upsert_reflector_dstar(refl)) {
                    dlog("[DB  ] upsert reflector.dstar failed");
                }
            }
        }
        // CLEAR via "Nicht verbunden"/"Not linked"/"Unlinked"
        else if (std::regex_search(line, mref, rx_dstar_unlinked)) {
            dlog("[PARSE] D-Star reflector cleared (unlinked)");
            if (!db.upsert_reflector_dstar(std::nullopt)) {
                dlog("[DB  ] clear reflector.dstar failed");
            }
        }
        // ALLES ANDERE IGNORIEREN (keine Aktion!)
    }


    if (changed) {
        if (!db.upsert_status(st)) dlog("[DB  ] upsert status failed");
    }
}

/* --------- Tail Loop --------- */

int main(int argc, char** argv) {
    std::string log_dir = "/var/log/mmdvm";
    if (argc >= 2) log_dir = argv[1];

    // DB env
    Db db;

    dbg_open();
    dlog("[MAIN] start. log_dir=", log_dir, " db=", db.name, "@", db.host);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // connect DB
    if (!db.connect()) {
        dlog("[DB  ] initial connect failed, will keep retrying in loop");
    }

    Status st;
    fs::path current = file_last_write_latest(log_dir);
    if (current.empty()) { dlog("[INIT] kein Log in ", log_dir); }
    else { dlog("[INIT] Logfile: ", current.string()); }

    // YSFGateway (neues zweites Log)
    std::regex pat_ysf(R"(YSFGateway-\d{4}-\d{2}-\d{2}\.log)");
    fs::path current_ysf = file_last_write_latest_with_pattern(log_dir, pat_ysf);

    std::ifstream ifs;
    std::ifstream ifs_ysf;

    // DMRGateway (drittes Log)
    std::regex pat_dmr(R"(DMRGateway-\d{4}-\d{2}-\d{2}\.log)");
    fs::path current_dmr = file_last_write_latest_with_pattern(log_dir, pat_dmr);

    std::ifstream ifs_dmr;

    auto open_and_seek_end = [&](const fs::path& p, std::ifstream& f, const char* tag){
        if (f.is_open()) f.close();
        f.open(p);
        if (!f) { dlog("[ERR ] kann Log nicht öffnen: ", p.string()); return false; }
        f.seekg(0, std::ios::end);
        dlog("[OPEN] ", tag, " seek end pos=", (long long)f.tellg());
        return true;
    };

    if (!current.empty()) {
        dlog("[INIT] MMDVM Logfile: ", current.string());
        open_and_seek_end(current, ifs, "MMDVM");
    } else {
        dlog("[INIT] kein MMDVM Log in ", log_dir);
    }

    if (!current_ysf.empty()) {
        dlog("[INIT] YSF Logfile: ", current_ysf.string());
        open_and_seek_end(current_ysf, ifs_ysf, "YSF ");
    } else {
        dlog("[INIT] kein YSF Log in ", log_dir);
    }

    if (!current_dmr.empty()) {
        dlog("[INIT] DMR Logfile: ", current_dmr.string());
        open_and_seek_end(current_dmr, ifs_dmr, "DMR ");
    } else {
        dlog("[INIT] kein DMR Log in ", log_dir);
    }

    std::string line;
    int idle_ticks = 0;
    int idle_ticks_ysf = 0;

    auto check_rotation_or_trunc = [&](){
        // MMDVM Rotation
        fs::path latest = file_last_write_latest(log_dir);
        if (!latest.empty() && latest != current) {
            dlog("[ROTA] MMDVM switch to ", latest.string());
            current = latest;
            open_and_seek_end(current, ifs, "MMDVM");
            idle_ticks = 0;
        }
        // MMDVM Truncation
        try {
            if (!current.empty()) {
                auto sz = fs::file_size(current);
                if ((std::uintmax_t)ifs.tellg() > sz) {
                    dlog("[TRNC] MMDVM file shorter, seek end");
                    ifs.clear(); ifs.seekg(0, std::ios::end);
                }
            }
        } catch (...) {}

        // YSF Rotation
        fs::path latest_ysf = file_last_write_latest_with_pattern(log_dir, pat_ysf);
        if (!latest_ysf.empty() && latest_ysf != current_ysf) {
            dlog("[ROTA] YSF  switch to ", latest_ysf.string());
            current_ysf = latest_ysf;
            open_and_seek_end(current_ysf, ifs_ysf, "YSF ");
            idle_ticks_ysf = 0;
        }
        // YSF Truncation
        try {
            if (!current_ysf.empty()) {
                auto sz = fs::file_size(current_ysf);
                if ((std::uintmax_t)ifs_ysf.tellg() > sz) {
                    dlog("[TRNC] YSF  file shorter, seek end");
                    ifs_ysf.clear(); ifs_ysf.seekg(0, std::ios::end);
                }
            }
        } catch (...) {}

        // DMR Rotation
        fs::path latest_dmr = file_last_write_latest_with_pattern(log_dir, pat_dmr);
        if (!latest_dmr.empty() && latest_dmr != current_dmr) {
            dlog("[ROTA] DMR  switch to ", latest_dmr.string());
            current_dmr = latest_dmr;
            open_and_seek_end(current_dmr, ifs_dmr, "DMR ");
        }
        // DMR Truncation
        try {
            if (!current_dmr.empty()) {
                auto sz = fs::file_size(current_dmr);
                if ((std::uintmax_t)ifs_dmr.tellg() > sz) {
                    dlog("[TRNC] DMR  file shorter, seek end");
                    ifs_dmr.clear(); ifs_dmr.seekg(0, std::ios::end);
                }
            }
        } catch (...) {}
    };

    while (running) {
        bool did_work = false;

        // --- MMDVM lesen (wie gehabt) ---
        if (ifs.good()) {
            std::string line;
            if (std::getline(ifs, line)) {
                dlog("[READ] ", line);
                handle_line_and_db(line, st, db);
                idle_ticks = 0;
                did_work = true;
            } else {
                ifs.clear();
                if (++idle_ticks % 10 == 0) { /* optional */ }
            }
        } else {
            dlog("[WARN] MMDVM stream bad; reopen in 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!current.empty()) open_and_seek_end(current, ifs, "MMDVM");
        }

        // --- YSFGateway lesen ---
        if (ifs_ysf.good()) {
            std::string line2;
            if (std::getline(ifs_ysf, line2)) {
                dlog("[READ][YSF ] ", line2);
                handle_line_ysfgw(line2, db);
                idle_ticks_ysf = 0;
                did_work = true;
            } else {
                ifs_ysf.clear();
                if (++idle_ticks_ysf % 10 == 0) { /* optional */ }
            }
        } else {
            // Nur warnen, wenn es eigentlich ein File geben sollte:
            if (!current_ysf.empty()) {
                dlog("[WARN] YSF  stream bad; reopen in 1s");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                open_and_seek_end(current_ysf, ifs_ysf, "YSF ");
            }
        }

        // --- DMRGateway lesen ---
        if (ifs_dmr.good()) {
            std::string line3;
            if (std::getline(ifs_dmr, line3)) {
                dlog("[READ][DMR ] ", line3);
                handle_line_dmrgw(line3, db);
                did_work = true;
            } else {
                ifs_dmr.clear();
            }
        } else {
            if (!current_dmr.empty()) {
                dlog("[WARN] DMR  stream bad; reopen in 1s");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                open_and_seek_end(current_dmr, ifs_dmr, "DMR ");
            }
        }

        // Wenn nix passiert ist, kurz schlafen, dann Rotation prüfen
        if (!did_work) std::this_thread::sleep_for(std::chrono::milliseconds(150));

        static int tick = 0;
        if (++tick % 10 == 0) check_rotation_or_trunc();

        // DB-keepalive
        db.ensure_conn();
    }

    // Bei Shutdown eine aktive DMR-TX sauber abschließen
    if (st.active && st.mode == "DMR") {
        dlog("[MAIN] finishing active DMR TX on shutdown");
        finish_tx_if_running(st, db, "shutdown");
    }

    dlog("[MAIN] exit");
    return 0;
}


/*
MMDVM.log
=========
D-Star:
Start (NET):
    M: 2025-10-27 15:41:34.380 D-Star, received network header from DB0SL   /INFO to CQCQCQ
Ende (NET):
    M: 2025-10-27 15:41:36.720 D-Star, received network end of transmission from DB0SL   /INFO to CQCQCQ  , 2.6 seconds, 0% packet loss, BER: 0.0%
Start (RF):
    M: 2025-10-27 16:03:16.227 D-Star, received RF header from DJ0ABR  /KURT to CQCQCQ
    oder
    M: 2025-10-27 16:03:24.861 D-Star, received RF late entry from DJ0ABR  /KURT to CQCQCQ
Ende (RF):
    M: 2025-10-27 16:03:19.547 D-Star, received RF end of transmission from DJ0ABR  /KURT to CQCQCQ  , 3.3 seconds, BER: 0.3%


YSF:
Start (NET):
    M: 2025-10-27 15:47:49.987 YSF, received network data from OE6JUD     to DG-ID 32 at OE6JUD
    oder
    M: 2025-10-27 16:35:36.014 YSF, received network data from OE5WAB     to DG-ID 32 at OE5XGL
Ende (NET):
    M: 2025-10-27 15:47:51.491 YSF, network watchdog has expired, 0.1 seconds, 0% packet loss, BER: 0.0%
    oder
    M: 2025-10-27 16:35:34.815 YSF, received network end of transmission from OE5WAB     to DG-ID 32, 1.6 seconds, 0% packet loss, BER: 0.0%
Start (RF):
    M: 2025-10-27 16:05:44.843 YSF, received RF header from DJ0ABR     to DG-ID 0
Ende (RF):
    M: 2025-10-27 16:05:47.144 YSF, received RF end of transmission from DJ0ABR     to DG-ID 0, 2.4 seconds, BER: 0.8%

DMR:
Start (NET):
    M: 2025-10-27 16:32:27.188 DMR Slot 2, received network voice header from DL3MX to TG 26386
Ende (NET):
    M: 2025-10-27 16:32:36.548 DMR Slot 2, received network end of voice transmission from DL3MX to TG 26386, 9.5 seconds, 0% packet loss, BER: 0.0%
Start (RF):
    M: 2025-10-27 16:06:11.560 DMR Slot 2, received RF voice header from DJ0ABR to TG 9
Ende (RF):
    M: 2025-10-27 16:06:14.978 DMR Slot 2, received RF end of voice transmission from DJ0ABR to TG 9, 3.2 seconds, BER: 0.0%

zusätzlich gültig für alle Betriebsarten: 
Ein End of Transmission wird immer erkannt wenn
* M: 2025-10-27 16:32:58.925 Mode set to Idle
* wenn ein neuer START kommt, obwohl zuvor das Ende gefehlt hat

*/
