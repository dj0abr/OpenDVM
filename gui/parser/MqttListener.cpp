// MqttListener.cpp
#include "MqttListener.h"
#include "fmdatabase.h"

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

// JSON (nlohmann)
#include <nlohmann/json.hpp>
using nlohmann::json;

bool MqttListener::init()
{
    if (s_initialized.load()) {
        std::cerr << "[MqttListener] Already initialized\n";
        return false;
    }

    mosquitto_lib_init();

    s_mosq = mosquitto_new(s_clientId.c_str(), true, nullptr);
    if (!s_mosq) {
        std::cerr << "[MqttListener] mosquitto_new() failed\n";
        mosquitto_lib_cleanup();
        return false;
    }

    mosquitto_connect_callback_set(s_mosq, &MqttListener::onConnect);
    mosquitto_message_callback_set(s_mosq, &MqttListener::onMessage);

    // eigene DB-Instanz
    s_db = new FMDatabase(); // Konstruktor verbindet + Schema
    s_initialized = true;
    return true;
}

void MqttListener::start()
{
    if (!s_initialized.load()) {
        std::cerr << "[MqttListener] Not initialized\n";
        return;
    }
    if (s_running.load()) {
        std::cerr << "[MqttListener] Already running\n";
        return;
    }

    s_running = true;
    s_thread = std::thread(&MqttListener::threadFunc);
}

void MqttListener::stop()
{
    if (!s_running.load())
        return;

    s_running = false;

    if (s_mosq) {
        mosquitto_disconnect(s_mosq);
    }

    if (s_thread.joinable()) {
        s_thread.join();
    }

    if (s_mosq) {
        mosquitto_destroy(s_mosq);
        s_mosq = nullptr;
    }

    mosquitto_lib_cleanup();

    // DB freigeben
    delete s_db;
    s_db = nullptr;

    s_initialized = false;
}

void MqttListener::threadFunc()
{
    std::cout << "[MqttListener] Connecting to " << s_host << ":" << s_port << "\n";

    int rc = mosquitto_connect(s_mosq, s_host.c_str(), s_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MqttListener] mosquitto_connect() failed: "
                  << mosquitto_strerror(rc) << "\n";
        s_running = false;
        return;
    }

    std::cout << "[MqttListener] Connected, waiting for messages...\n";

    while (s_running.load()) {
        rc = mosquitto_loop(s_mosq, -1, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MqttListener] mosquitto_loop() error: "
                      << mosquitto_strerror(rc) << " -> reconnect\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));

            rc = mosquitto_reconnect(s_mosq);
            if (rc != MOSQ_ERR_SUCCESS) {
                std::cerr << "[MqttListener] reconnect failed: "
                          << mosquitto_strerror(rc) << "\n";
            }
        }
    }

    std::cout << "[MqttListener] Thread exiting\n";
}

void MqttListener::onConnect(struct mosquitto* /*mosq*/, void* /*userdata*/, int rc)
{
    std::cout << "[MqttListener] onConnect rc=" << rc << "\n";
    if (rc == 0) {
        std::cout << "[MqttListener] Subscribing to topic: " << s_topic << "\n";
        int subRc = mosquitto_subscribe(s_mosq, nullptr, s_topic.c_str(), 0);
        if (subRc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MqttListener] subscribe failed: "
                      << mosquitto_strerror(subRc) << "\n";
        }
    } else {
        std::cerr << "[MqttListener] Connect failed, rc=" << rc << "\n";
    }
}

void MqttListener::onMessage(struct mosquitto* /*mosq*/,
                             void* /*userdata*/,
                             const struct mosquitto_message* msg)
{
    std::string topic;
    if (msg->topic) topic = msg->topic;

    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
        payload.assign(static_cast<char*>(msg->payload),
                       static_cast<size_t>(msg->payloadlen));
    }

    std::cout << "[MQTT] Topic: " << topic
              << " | QoS: " << msg->qos
              << " | Retain: " << msg->retain
              << " | Payload: " << payload << "\n";

    // Nur die Talker-Events /server/statethr/.. in DB schreiben
    if (topic.rfind("/server/statethr", 0) == 0 && s_db) {
        try {
            json j = json::parse(payload);

            std::string timeStr  = j.value("time",   "");
            std::string talkStr  = j.value("talk",   "");
            std::string callStr  = j.value("call",   "");
            std::string tgStr    = j.value("tg",     "");
            std::string srvStr   = j.value("server", "");

            if (!timeStr.empty() && !talkStr.empty() && !callStr.empty() && !tgStr.empty()) {
                if (!s_db->insertEvent(timeStr, talkStr, callStr, tgStr, srvStr)) {
                    std::cerr << "[MqttListener] insertEvent failed\n";
                }
            } else {
                std::cerr << "[MqttListener] JSON missing required fields\n";
            }

        } catch (const std::exception& e) {
            std::cerr << "[MqttListener] JSON parse error: " << e.what() << "\n";
        }
    }
}
