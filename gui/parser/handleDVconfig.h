#pragma once
#include <string>
#include "renderConfigFile.h"

/**
 * handleDVconfig:
 * Holds persistent renderers for source configs and exposes parsed site data as a struct.
 */
struct siteData {
    // host [General]
    std::string Callsign;
    std::string Id;
    std::string Duplex;

    // host [Info]
    std::string RXFrequency;
    std::string TXFrequency;
    std::string Longitude;
    std::string Latitude;
    std::string Height;
    std::string Location;
    std::string Description;
    std::string URL;

    // host [D-Star]
    std::string Module;

    // ircddb [] (flat)
    std::string reflector1;

    // ysf
    std::string Suffix;   // [General]
    std::string Startup;  // [Network]
    std::string Options;  // [Network]

    // dmr [DMR Network 1]
    std::string Address;
    std::string Password;
    std::string Name;
};

class handleDVconfig {
public:
    handleDVconfig();

    /** Read all source configs and populate 'site'. */
    void readConfig();

    bool saveConfig();

    // Result object with all mapped fields
    siteData site;

private:
    // Persistent source renderers
    renderConfigFile host_renderer;
    renderConfigFile ircddb_renderer;
    renderConfigFile dmr_renderer;
    renderConfigFile ysf_renderer;
};