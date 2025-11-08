#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include "Database.h"
#include "handleDVconfig.h"

// Close on destruction
Database::~Database() {
    if (conn_) { mysql_close(conn_); conn_ = nullptr; }
}

// Connect at construction
Database::Database() {
    if (!connect()) {
        std::fprintf(stderr, "[DB] initial connect() failed: %s\n", last_error_.c_str());
    }
}

// Simple query wrapper
bool Database::exec(const char* sql) noexcept {
    if (!conn_) {
        last_error_ = "exec() without connection";
        std::fprintf(stderr, "[DB] %s\n", last_error_.c_str());
        return false;
    }
    if (mysql_query(conn_, sql) == 0) return true;
    last_error_ = mysql_error(conn_);
    std::fprintf(stderr, "[DB] query failed: %s\n", last_error_.c_str());
    return false;
}

// Ensure connection is alive
bool Database::ensure_conn() noexcept {
    if (!conn_) {
        bool ok = connect();
        if (!ok) std::fprintf(stderr, "[DB] ensure_conn: connect failed: %s\n", last_error_.c_str());
        return ok;
    }
    if (mysql_ping(conn_) == 0) return true;
    std::fprintf(stderr, "[DB] ping failed: %s -> reconnect\n", mysql_error(conn_));
    bool ok = connect();
    if (!ok) std::fprintf(stderr, "[DB] reconnect failed: %s\n", last_error_.c_str());
    return ok;
}

// Open socket connection and ensure schema
bool Database::connect() noexcept {
    last_error_.clear();
    if (conn_) { mysql_close(conn_); conn_ = nullptr; }

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        last_error_ = "mysql_init failed";
        std::fprintf(stderr, "[DB] %s\n", last_error_.c_str());
        return false;
    }

    // optional auto-reconnect (use bool for modern headers)
    {
        bool rc = true;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &rc);
    }
    // force Unix-socket protocol
    {
        unsigned int proto = MYSQL_PROTOCOL_SOCKET;
        mysql_options(conn_, MYSQL_OPT_PROTOCOL, &proto);
    }

    if (!mysql_real_connect(conn_, /*host*/nullptr, user_.c_str(), /*pass*/"",
                            name_.c_str(), port_, unix_socket_.c_str(), /*flags*/0)) {
        last_error_ = mysql_error(conn_);
        std::fprintf(stderr, "[DB] mysql_real_connect failed: %s\n", last_error_.c_str());
        mysql_close(conn_); conn_ = nullptr;
        return false;
    }

    if (!createTableIfNeeded()) {
        std::fprintf(stderr, "[DB] createTableIfNeeded failed: %s\n", last_error_.c_str());
        return false;
    }
    if (!ensureSingleRow()) {
        std::fprintf(stderr, "[DB] ensureSingleRow failed: %s\n", last_error_.c_str());
        return false;
    }
    return true;
}

// Create table if it doesn't exist — all payload columns as VARCHAR(255), id tinyint, updated_at DATETIME auto
bool Database::createTableIfNeeded() noexcept {
    static const char* q_cfg =
        "CREATE TABLE IF NOT EXISTS config_inbox ("
        " id TINYINT UNSIGNED NOT NULL PRIMARY KEY,"
        " callsign         VARCHAR(255)     NOT NULL,"
        " module           VARCHAR(255)     NOT NULL,"
        " dmr_id           VARCHAR(255)     NOT NULL,"
        " duplex           VARCHAR(255)     NOT NULL,"
        " rxfreq           VARCHAR(255)     NOT NULL,"
        " txfreq           VARCHAR(255)     NOT NULL,"
        " latitude         VARCHAR(255)     NOT NULL,"
        " longitude        VARCHAR(255)     NOT NULL,"
        " height           VARCHAR(255)     NULL,"
        " location         VARCHAR(255)     NULL,"
        " description      VARCHAR(255)     NULL,"
        " url              VARCHAR(255)     NULL,"
        " reflector1       VARCHAR(255)     NULL,"
        " ysf_suffix       VARCHAR(255)     NULL,"
        " ysf_startup      VARCHAR(255)     NULL,"
        " ysf_options      VARCHAR(255)     NULL,"
        " dmr_address      VARCHAR(255)     NULL,"
        " dmr_password     VARCHAR(255)     NULL,"
        " dmr_name         VARCHAR(255)     NULL,"
        " is_new           VARCHAR(255)     NOT NULL DEFAULT 'IDLE',"
        " updated_at       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
    return exec(q_cfg);
}

// Ensure exactly one row exists (id=1). Insert defaults if empty using INSERT ... SET to avoid count mismatch
bool Database::ensureSingleRow() noexcept {
    if (mysql_query(conn_, "SELECT COUNT(*) FROM config_inbox") != 0) {
        last_error_ = mysql_error(conn_);
        std::fprintf(stderr, "[DB] COUNT(*) failed: %s\n", last_error_.c_str());
        return false;
    }
    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        last_error_ = mysql_error(conn_);
        std::fprintf(stderr, "[DB] store_result failed: %s\n", last_error_.c_str());
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    unsigned long long cnt = (row && row[0]) ? std::strtoull(row[0], nullptr, 10) : 0ULL;
    mysql_free_result(res);

    if (cnt == 0ULL) {
        static const char* ins =
            "INSERT INTO config_inbox SET "
            "id=1,"
            "callsign='',"
            "module=''," 
            "dmr_id='',"
            "duplex='',"
            "rxfreq='',"
            "txfreq='',"
            "latitude='',"
            "longitude='',"
            "height=NULL,"
            "location=NULL,"
            "description=NULL,"
            "url=NULL,"
            "reflector1=NULL,"
            "ysf_suffix=NULL,"
            "ysf_startup=NULL,"
            "ysf_options=NULL,"
            "dmr_address=NULL,"
            "dmr_password=NULL,"
            "dmr_name=NULL,"
            "is_new='IDLE'";
        if (!exec(ins)) {
            std::fprintf(stderr, "[DB] insert default row failed: %s\n", last_error_.c_str());
            return false;
        }
    }
    return true;
}

// Write site data to id=1 (escape all values safely); updated_at auto by DB
bool Database::writeSiteData(const siteData& s) noexcept {
    if (!ensure_conn()) {
        std::fprintf(stderr, "[DB] writeSiteData: no connection: %s\n", last_error_.c_str());
        return false;
    }

    auto esc = [&](const std::string& v)->std::string {
        if (!conn_) return std::string();
        std::string out; out.resize(v.size()*2 + 1);
        unsigned long n = mysql_real_escape_string(conn_, &out[0], v.c_str(), (unsigned long)v.size());
        out.resize(n);
        return out;
    };

    std::string q = "UPDATE config_inbox SET ";
    q += "callsign='"          + esc(s.Callsign)    + "',";
    q += "module='"            + esc(s.Module)      + "',";
    q += "dmr_id='"            + esc(s.Id)          + "',";
    q += "duplex='"            + esc(s.Duplex)      + "',";
    q += "rxfreq='"            + esc(s.RXFrequency) + "',";
    q += "txfreq='"            + esc(s.TXFrequency) + "',";
    q += "latitude='"          + esc(s.Latitude)    + "',";
    q += "longitude='"         + esc(s.Longitude)   + "',";
    q += "height='"            + esc(s.Height)      + "',";
    q += "location='"          + esc(s.Location)    + "',";
    q += "description='"       + esc(s.Description) + "',";
    q += "url='"               + esc(s.URL)         + "',";
    q += "reflector1='"        + esc(s.reflector1)  + "',";
    q += "ysf_suffix='"        + esc(s.Suffix)      + "',";
    q += "ysf_startup='"       + esc(s.Startup)     + "',";
    q += "ysf_options='"       + esc(s.Options)     + "',";
    q += "dmr_address='"       + esc(s.Address)     + "',";
    q += "dmr_password='"      + esc(s.Password)    + "',";
    q += "dmr_name='"          + esc(s.Name)        + "',";
    q += "is_new='BACKEND' ";
    q += "WHERE id=1";

    if (!exec(q.c_str())) {
        std::fprintf(stderr, "[DB] writeSiteData UPDATE failed: %s\n", last_error_.c_str());
        return false;
    }
    return true;
}

bool Database::readSiteData(siteData& s) noexcept {
    if (!ensure_conn()) {
        std::fprintf(stderr, "[DB] readSiteData: no connection: %s\n", last_error_.c_str());
        return false;
    }

    static const char* q =
        "SELECT "
        " callsign,"          // 0
        " module,"            // 1
        " dmr_id,"            // 2
        " duplex,"            // 3
        " rxfreq,"            // 4
        " txfreq,"            // 5
        " latitude,"          // 6
        " longitude,"         // 7
        " height,"            // 8
        " location,"          // 9
        " description,"       // 10
        " url,"               // 11
        " reflector1,"        // 12
        " ysf_suffix,"        // 13
        " ysf_startup,"       // 14
        " ysf_options,"       // 15
        " dmr_address,"       // 16
        " dmr_password,"      // 17
        " dmr_name,"          // 18
        " is_new"             // 19
        " FROM config_inbox WHERE id=1 LIMIT 1";

    if (mysql_query(conn_, q) != 0) {
        last_error_ = mysql_error(conn_);
        std::fprintf(stderr, "[DB] readSiteData query failed: %s\n", last_error_.c_str());
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        last_error_ = mysql_error(conn_);
        std::fprintf(stderr, "[DB] readSiteData store_result failed: %s\n", last_error_.c_str());
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        last_error_ = "readSiteData: no row with id=1";
        std::fprintf(stderr, "[DB] %s\n", last_error_.c_str());
        return false;
    }

    unsigned long* len = mysql_fetch_lengths(res);
    auto get = [&](int i) -> std::string {
        return row[i] ? std::string(row[i], len ? len[i] : std::strlen(row[i])) : std::string();
    };

    // Check: is_new muss "GUI" enthalten
    const std::string is_new = get(19);
    if (is_new.find("GUI") == std::string::npos) {
        mysql_free_result(res);
        //last_error_ = "readSiteData: is_new != GUI (current: '" + is_new + "')";
        //std::fprintf(stderr, "[DB] %s\n", last_error_.c_str());
        return false;
    }

    // Jetzt Struktur befüllen
    s.Callsign     = get(0);
    s.Module       = get(1);
    s.Id           = get(2);
    s.Duplex       = get(3);
    s.RXFrequency  = get(4);
    s.TXFrequency  = get(5);
    s.Latitude     = get(6);
    s.Longitude    = get(7);
    s.Height       = get(8);
    s.Location     = get(9);
    s.Description  = get(10);
    s.URL          = get(11);
    s.reflector1   = get(12);
    s.Suffix       = get(13);
    s.Startup      = get(14);
    s.Options      = get(15);
    s.Address      = get(16);
    s.Password     = get(17);
    s.Name         = get(18);

    // Result vor dem nächsten Query freigeben
    mysql_free_result(res);

    // Status auf IDLE setzen (Pflicht — sonst false)
    if (!exec("UPDATE config_inbox SET is_new='IDLE' WHERE id=1")) {
        std::fprintf(stderr, "[DB] readSiteData: set is_new=IDLE failed: %s\n", last_error_.c_str());
        return false;
    }

    return true;
}
