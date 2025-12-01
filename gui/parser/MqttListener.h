// MqttListener.hpp
#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mosquitto.h>

class FMDatabase; // forward

class MqttListener {
public:
    static bool init();

    static void start();
    static void stop();

private:
    MqttListener() = delete;

    static void threadFunc();

    static void onConnect(struct mosquitto* mosq, void* userdata, int rc);
    static void onMessage(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg);

    static inline std::string s_host = "mqtt.fm-funknetz.de";
    static inline int         s_port = 1883;
    static inline std::string s_topic = "/server/statethr/1/#";
    static inline std::string s_clientId = "fm-monitor";

    static inline std::thread        s_thread;
    static inline std::atomic<bool>  s_running{false};
    static inline struct mosquitto*  s_mosq = nullptr;
    static inline std::atomic<bool>  s_initialized{false};

    // eigene DB-Instanz
    static inline FMDatabase*        s_db = nullptr;
};
