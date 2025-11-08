#pragma once
#include <string>
#include <mysql/mysql.h>

struct siteData; // forward declaration

/**
 * Opens a local MySQL/MariaDB via Unix-Socket and ensures schema/single row.
 * Note: All payload fields are stored as VARCHAR(255). Only 'id' remains numeric (TINYINT UNSIGNED).
 */
class Database {
public:
    Database();
    ~Database();

    // Ensure connection is alive (reconnects if needed)
    bool ensure_conn() noexcept;

    // Write site data (id=1 row) into config_inbox
    bool writeSiteData(const siteData& s) noexcept;

    // Read site data (id=1) aus config_inbox in s
    bool readSiteData(struct siteData& s) noexcept;

    // Last error text (empty if none)
    const std::string& lastError() const noexcept { return last_error_; }

private:
    bool connect() noexcept;
    bool exec(const char* sql) noexcept;
    bool createTableIfNeeded() noexcept;
    bool ensureSingleRow() noexcept;

private:
    std::string host_        = "localhost";
    std::string user_        = "mmdvm";
    std::string pass_        = "";
    std::string name_        = "mmdvmdb";
    unsigned int port_       = 0;
    std::string unix_socket_ = "/run/mysqld/mysqld.sock";

    MYSQL* conn_ = nullptr;
    std::string last_error_;
};