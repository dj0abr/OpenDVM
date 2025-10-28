#include <bits/stdc++.h>
#include <ctime>
#include <sys/stat.h>
using namespace std;

static inline string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static inline bool starts_with(const string& s, const string& p) { return s.rfind(p, 0) == 0; }

static string now_stamp() {
    time_t t = time(nullptr);
    tm tmv{};
    localtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

static bool file_exists(const string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static vector<string> read_lines(const string& path) {
    ifstream f(path);
    if (!f) throw runtime_error("kann Datei nicht lesen: " + path);
    vector<string> lines;
    string line;
    while (getline(f, line)) lines.push_back(line);
    return lines;
}

static void write_lines_atomic(const string& path, const vector<string>& lines) {
    string tmp = path + ".tmp";
    {
        ofstream f(tmp, ios::trunc);
        if (!f) throw runtime_error("kann Datei nicht schreiben: " + tmp);
        for (size_t i = 0; i < lines.size(); ++i) {
            f << lines[i];
            if (i + 1 < lines.size()) f << "\n";
        }
    }
    string bak = path + ".bak-" + now_stamp();
    if (file_exists(path)) rename(path.c_str(), bak.c_str());
    rename(tmp.c_str(), path.c_str());
}

static string strip_inline_comment_preserve_quotes(const string& in) {
    bool in_single = false, in_double = false, esc = false;
    string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (esc) { out.push_back(c); esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '\'' && !in_double) { in_single = !in_single; out.push_back(c); continue; }
        if (c == '"'  && !in_single) { in_double = !in_double; out.push_back(c); continue; }
        if (c == '#' && !in_single && !in_double) break;
        out.push_back(c);
    }
    return out;
}

static string unquote(const string& v) {
    if (v.size() >= 2) {
        if ((v.front()=='"' && v.back()=='"') || (v.front()=='\'' && v.back()=='\'')) {
            return v.substr(1, v.size()-2);
        }
    }
    return v;
}

static string unescape_backslashes_and_hash(const string& v) {
    string o; o.reserve(v.size());
    bool esc = false;
    for (char c : v) {
        if (esc) {
            if (c == '#' || c == '\\') o.push_back(c);
            else { o.push_back('\\'); o.push_back(c); }
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else {
            o.push_back(c);
        }
    }
    if (esc) o.push_back('\\');
    return o;
}

static map<string,string> read_site_conf(const string& path) {
    ifstream f(path);
    if (!f) throw runtime_error("site.conf fehlt: " + path);
    map<string,string> m;
    string line;
    while (getline(f, line)) {
        string s = trim(line);
        if (s.empty() || s[0]=='#') continue;
        auto eq = s.find('=');
        if (eq == string::npos) continue;

        string k = trim(s.substr(0, eq));
        string v = trim(s.substr(eq+1));

        v = strip_inline_comment_preserve_quotes(v);
        v = trim(v);
        v = unquote(v);
        v = unescape_backslashes_and_hash(v);

        m[k] = v;
    }
    return m;
}

static string to_fixed6(double x) { ostringstream o; o.setf(std::ios::fixed); o<<setprecision(6)<<x; return o.str(); }

static void set_kv_replace_all(vector<string>& lines, const string& key, const string& value) {
    string prefix = key + "="; bool found = false;
    for (auto& L : lines) {
        string t = trim(L);
        if (!t.empty() && t[0]=='#') continue;
        if (starts_with(t, prefix)) { L = key + "=" + value; found = true; }
    }
    if (!found) lines.push_back(key + "=" + value);
}

static void set_kv_replace_first_or_append(vector<string>& lines, const string& key, const string& value) {
    string prefix = key + "=";
    for (auto& L : lines) {
        string t = trim(L);
        if (!t.empty() && t[0]=='#') continue;
        if (starts_with(t, prefix)) { L = key + "=" + value; return; }
    }
    lines.push_back(key + "=" + value);
}

// ersetzt Key in Section NUR wenn der Key dort bereits existiert – fügt NICHT neu hinzu
static bool replace_kv_in_section_if_present(vector<string>& lines, const string& section, const string& key, const string& value) {
    string sec_hdr = "[" + section + "]";
    int sec_start=-1, sec_end=(int)lines.size();
    for (int i=0;i<(int)lines.size();++i) {
        string t=trim(lines[i]);
        if (starts_with(t,"[")) {
            if (t==sec_hdr) {
                sec_start=i;
                for (int j=i+1;j<(int)lines.size();++j) {
                    string u=trim(lines[j]);
                    if (starts_with(u,"[")) {sec_end=j; break;}
                }
                break;
            }
        }
    }
    if (sec_start==-1) return false;
    string prefix=key+"=";
    for (int i=sec_start+1;i<sec_end;++i) {
        string t=trim(lines[i]);
        if (!t.empty()&&t[0]=='#') continue;
        if (starts_with(t,prefix)) { lines[i]=key+"="+value; return true; }
    }
    return false;
}

// ersetzt in Section, fügt falls nicht vorhanden am Ende der Section hinzu
static void set_kv_in_section(vector<string>& lines, const string& section, const string& key, const string& value) {
    string sec_hdr = "[" + section + "]";
    int sec_start=-1, sec_end=(int)lines.size();
    for (int i=0;i<(int)lines.size();++i) {
        string t=trim(lines[i]);
        if (starts_with(t,"[")) {
            if (t==sec_hdr) {
                sec_start=i;
                for (int j=i+1;j<(int)lines.size();++j) {
                    string u=trim(lines[j]);
                    if (starts_with(u,"[")) {sec_end=j; break;}
                }
                break;
            }
        }
    }
    if (sec_start==-1) { lines.push_back(""); lines.push_back(sec_hdr); lines.push_back(key+"="+value); return; }
    string prefix=key+"=";
    for (int i=sec_start+1;i<sec_end;++i) {
        string t=trim(lines[i]);
        if (!t.empty()&&t[0]=='#') continue;
        if (starts_with(t,prefix)) { lines[i]=key+"="+value; return; }
    }
    lines.insert(lines.begin()+sec_end,key+"="+value);
}

static string normalize_url(const string& url) {
    if (url.empty()) return url;
    auto low=url; for(auto&c:low)c=tolower(c);
    if(low.rfind("http://",0)==0||low.rfind("https://",0)==0) return url;
    return "http://"+url;
}

// ersetzt alle "Id=" Vorkommen (egal in welcher Section), wenn vorhanden – fügt NICHT neu hinzu
static bool replace_all_Id_if_present(vector<string>& lines, const string& value) {
    bool any=false;
    string prefix="Id=";
    for (auto& L : lines) {
        string t=trim(L);
        if (!t.empty() && t[0]=='#') continue;
        if (starts_with(t,prefix)) { L = "Id=" + value; any=true; }
    }
    return any;
}

int main() try {
    const string site_path = "./site.conf";
    auto env = read_site_conf(site_path);

    auto val=[&](const string&k,const string&d=""){auto it=env.find(k);return it==env.end()?d:it->second;};
    auto to_i64=[&](const string&k){try{return stoll(val(k,"0"));}catch(...){return 0LL;}};

    const string Callsign=val("Callsign"), Module=val("Module","B"), Id=val("Id","0"), Duplex=val("Duplex","1");
    const long long RX_Hz=to_i64("RXFrequency"), TX_Hz=to_i64("TXFrequency");
    const string Latitude=val("Latitude","0.0"), Longitude=val("Longitude","0.0"), Height=val("Height","0");
    const string Location=val("Location",""), Description=val("Description",""), URL=normalize_url(val("URL",""));
    const string reflector1=val("reflector1",""), Suffix=val("Suffix",""), Startup=val("Startup","");
    const string Address=val("Address",""), Password=val("Password",""), Name=val("Name","");

    const double RX_MHz=RX_Hz/1e6, TX_MHz=TX_Hz/1e6;
    // Offset soll negativ sein -> RX - TX
    const double OFFSET_MHz=(RX_Hz - TX_Hz)/1e6;

    // MMDVMHost.ini
    {
        const string path="/etc/MMDVMHost.ini";
        auto lines=read_lines(path);
        set_kv_replace_all(lines,"Callsign",Callsign);
        set_kv_replace_all(lines,"Id",Id);
        set_kv_replace_all(lines,"Duplex",Duplex);
        set_kv_replace_all(lines,"RXFrequency",to_string(RX_Hz));
        set_kv_replace_all(lines,"TXFrequency",to_string(TX_Hz));
        set_kv_replace_all(lines,"Latitude",Latitude);
        set_kv_replace_all(lines,"Longitude",Longitude);
        set_kv_replace_all(lines,"Height",Height);
        set_kv_replace_all(lines,"Location","\""+Location+"\"");
        set_kv_replace_all(lines,"Description","\""+Description+"\"");
        set_kv_replace_all(lines,"URL",URL);
        set_kv_replace_all(lines,"Module",Module);
        write_lines_atomic(path,lines);
        cout<<"✓ aktualisiert: "<<path<<"\n";
    }

    // ircddbgateway
    {
        const string path="/etc/ircddbgateway";
        auto lines=read_lines(path);
        set_kv_replace_first_or_append(lines,"gatewayCallsign",Callsign);
        set_kv_replace_first_or_append(lines,"latitude",Latitude);
        set_kv_replace_first_or_append(lines,"longitude",Longitude);
        set_kv_replace_first_or_append(lines,"description1",Location);
        set_kv_replace_first_or_append(lines,"description2",Description);
        set_kv_replace_first_or_append(lines,"url",URL);
        set_kv_replace_first_or_append(lines,"repeaterCall1",Callsign);
        set_kv_replace_first_or_append(lines,"repeaterBand1",Module);
        if(!reflector1.empty())set_kv_replace_first_or_append(lines,"reflector1",reflector1);
        set_kv_replace_first_or_append(lines,"frequency1",to_fixed6(TX_MHz));
        set_kv_replace_first_or_append(lines,"offset1",to_fixed6(OFFSET_MHz)); // jetzt negativ
        set_kv_replace_first_or_append(lines,"latitude1",Latitude);
        set_kv_replace_first_or_append(lines,"longitude1",Longitude);
        set_kv_replace_first_or_append(lines,"description1_1",Location);
        set_kv_replace_first_or_append(lines,"description1_2",Description);
        set_kv_replace_first_or_append(lines,"url1",URL);
        set_kv_replace_first_or_append(lines,"ircddbUsername",Callsign);
        set_kv_replace_first_or_append(lines,"dplusLogin",Callsign);
        // falls Id in dieser Datei existiert, überall ersetzen
        replace_all_Id_if_present(lines, Id);
        write_lines_atomic(path,lines);
        cout<<"✓ aktualisiert: "<<path<<"\n";
    }

    // ysfgateway
    {
        const string path="/etc/ysfgateway";
        auto lines=read_lines(path);
        string YSF_Name=Callsign+"_OpenDVM";
        set_kv_replace_all(lines,"Callsign",Callsign);
        set_kv_replace_all(lines,"Suffix",Suffix);
        set_kv_replace_all(lines,"Id",Id); // überall ersetzen, falls mehrfach
        set_kv_replace_all(lines,"RXFrequency",to_string(RX_Hz));
        set_kv_replace_all(lines,"TXFrequency",to_string(TX_Hz));
        set_kv_replace_all(lines,"Latitude",Latitude);
        set_kv_replace_all(lines,"Longitude",Longitude);
        set_kv_replace_all(lines,"Height",Height);
        set_kv_replace_all(lines,"Name",YSF_Name);
        set_kv_replace_all(lines,"Description","\""+Description+"\""); // <-- Quotes
        if(!Startup.empty())set_kv_replace_first_or_append(lines,"Startup",Startup);
        write_lines_atomic(path,lines);
        cout<<"✓ aktualisiert: "<<path<<"\n";
    }

    // dmrgateway
    {
        const string path="/etc/dmrgateway";
        auto lines=read_lines(path);

        // [General] -> NICHTS neu hinzufügen, nur ersetzen falls vorhanden
        replace_kv_in_section_if_present(lines,"General","RXFrequency",to_string(RX_Hz));
        replace_kv_in_section_if_present(lines,"General","TXFrequency",to_string(TX_Hz));
        replace_kv_in_section_if_present(lines,"General","Latitude",Latitude);
        replace_kv_in_section_if_present(lines,"General","Longitude",Longitude);
        replace_kv_in_section_if_present(lines,"General","Height",Height);
        replace_kv_in_section_if_present(lines,"General","Location","\""+Location+"\"");
        replace_kv_in_section_if_present(lines,"General","Description","\""+Description+"\"");
        replace_kv_in_section_if_present(lines,"General","URL",URL);

        // [Info] -> hier ist Ergänzen ok; zusätzlich Quotes für Location/Description
        set_kv_in_section(lines,"Info","RXFrequency",to_string(RX_Hz));
        set_kv_in_section(lines,"Info","TXFrequency",to_string(TX_Hz));
        set_kv_in_section(lines,"Info","Latitude",Latitude);
        set_kv_in_section(lines,"Info","Longitude",Longitude);
        set_kv_in_section(lines,"Info","Height",Height);
        set_kv_in_section(lines,"Info","Location","\""+Location+"\"");
        set_kv_in_section(lines,"Info","Description","\""+Description+"\"");
        set_kv_in_section(lines,"Info","URL",URL);

        // [DMR Network 1]
        set_kv_in_section(lines,"DMR Network 1","Address",Address);
        set_kv_in_section(lines,"DMR Network 1","Password","\""+Password+"\"");
        set_kv_in_section(lines,"DMR Network 1","Id",Id);
        set_kv_in_section(lines,"DMR Network 1","Name",Name);

        // falls Id woanders existiert, global überall ersetzen (ohne neue Ids zu erzeugen)
        replace_all_Id_if_present(lines, Id);

        write_lines_atomic(path,lines);
        cout<<"✓ aktualisiert: "<<path<<"\n";
    }

    cout<<"Alles fertig. Backups: *.bak-<timestamp>\n";
    return 0;

} catch(const exception&e){ cerr<<"FEHLER: "<<e.what()<<"\n"; return 1; }
