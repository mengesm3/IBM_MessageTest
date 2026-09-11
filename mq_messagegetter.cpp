#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <imqi.hpp>

int main() {
    ImqQueueManager manager;
    manager.setName("QM1");

    ImqChannel channel;
    channel.setChannelName("DEV.APP.SVRCONN");
    channel.setTransportType(MQXPT_TCP);
    channel.setConnectionName("ibmmq(1414)");
    manager.setChannelReference(&channel);

    manager.setUserId("app");
    manager.setPassword("passw0rd");
    manager.setAuthenticationType(MQCSP_AUTH_USER_ID_AND_PWD);

    const int maxAttempts = 15;
    const int retryDelaySeconds = 3;
    bool connected = false;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        std::cout << "Connection attempt " << attempt << " of " << maxAttempts
                  << "..." << std::endl;
        if (manager.connect()) {
            connected = true;
            break;
        }
        std::cerr << "  Attempt " << attempt << " failed. Reason: "
                  << manager.reasonCode() << std::endl;
        if (attempt < maxAttempts) {
            sleep(retryDelaySeconds);
        }
    }

    if (!connected) {
        std::cerr << "Failed to connect after " << maxAttempts
                  << " attempts. Last reason code: " << manager.reasonCode()
                  << std::endl;
        return 1;
    }
    std::cout << "Connected to Queue Manager: " << manager.name() << std::endl;

    ImqQueue queue;
    queue.setConnectionReference(manager);
    queue.setName("DEV.QUEUE.1");
    queue.setOpenOptions(MQOO_INPUT_AS_Q_DEF | MQOO_FAIL_IF_QUIESCING);
    queue.open();

    if (queue.completionCode() == MQCC_FAILED) {
        std::cerr << "Unable to open queue for input. Reason: "
                  << queue.reasonCode() << std::endl;
        manager.disconnect();
        return 1;
    }

    // Wait up to 5 seconds for a message if the queue is currently empty.
    ImqGetMessageOptions gmo;
    gmo.setOptions(MQGMO_WAIT | MQGMO_FAIL_IF_QUIESCING);
    gmo.setWaitInterval(5000);

    ImqMessage msg;
    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    msg.useEmptyBuffer(buffer, sizeof(buffer) - 1);

    if (queue.get(msg, gmo)) {
        std::cout << "Received message (" << msg.bufferLength()
                  << " bytes): " << buffer << std::endl;
    } else {
        std::cerr << "Failed to get message. Reason Code: "
                  << queue.reasonCode() << std::endl;
    }

    queue.close();
    manager.disconnect();

    return 0;
}
