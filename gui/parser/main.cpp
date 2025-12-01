#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include "handleDVconfig.h"
#include "Database.h"
#include "MqttListener.h"

static std::atomic<bool> g_running{true};

void sigHandler(int)
{
    g_running = false;
    MqttListener::stop();
}

int main(){
    // Nach dem Programmstart fülle die Datenbank einmalig
    handleDVconfig dv;
    dv.readConfig();           // fills dv.site
    Database db;
    db.writeSiteData(dv.site); // push to DB (id=1)

    // Starte FM Funknetz Abfragen als Thread
    MqttListener::init();
    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);
    MqttListener::start();

    // führe DV Logfile Parsing in dieser Loop aus
    while(g_running) {
        bool newdata = db.readSiteData(dv.site);
        if(newdata) {
            // handle data
            printf("new data from GUI\n");
            dv.saveConfig();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
