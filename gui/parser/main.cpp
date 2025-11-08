#include <thread>
#include <chrono>
#include "handleDVconfig.h"
#include "Database.h"

int main(){
    // Nach dem Programmstart fülle die Datenbank einmalig
    handleDVconfig dv;
    dv.readConfig();           // fills dv.site
    Database db;
    db.writeSiteData(dv.site); // push to DB (id=1)

    // jetzt mache eine Loop um Änderungen des GUIs auszuführen
    while(true) {
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
