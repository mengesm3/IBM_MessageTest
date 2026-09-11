#include <iostream>
#include <string>
#include <unistd.h>
#include <imqi.hpp>

int main() {
    ImqQueueManager manager;
    manager.setName("QM1");

    // Explicit channel configuration (bypasses MQSERVER env var entirely)
    ImqChannel channel;
    channel.setChannelName("DEV.APP.SVRCONN");
    channel.setTransportType(MQXPT_TCP);
    channel.setConnectionName("ibmmq(1414)");
    manager.setChannelReference(&channel);

    // The DEV.APP.SVRCONN channel requires a user ID and password.
    // setUserId/setPassword alone only store the values - they are not
    // actually transmitted unless MQCSP authentication is explicitly
    // enabled via setAuthenticationType (it defaults to MQCSP_AUTH_NONE).
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
            std::cerr << "  Retrying in " << retryDelaySeconds
                      << " seconds..." << std::endl;
            sleep(retryDelaySeconds);
        }
    }

    if (!connected) {
        std::cerr << "Failed to connect to Queue Manager after "
                  << maxAttempts << " attempts. Last reason code: "
                  << manager.reasonCode() << std::endl;
        return 1;
    }
    std::cout << "Connected to Queue Manager: " << manager.name() << std::endl;

    ImqQueue queue;
    queue.setConnectionReference(manager);
    queue.setName("DEV.QUEUE.1");

    queue.setOpenOptions(MQOO_OUTPUT | MQOO_FAIL_IF_QUIESCING);
    queue.open();

    if (queue.completionCode() == MQCC_FAILED) {
        std::cerr << "Unable to open queue for output. Reason: "
                  << queue.reasonCode() << std::endl;
        manager.disconnect();
        return 1;
    }

    ImqMessage msg;
    std::string payload = "Hello world from IBM MQ C++ Message Client";

    msg.useFullBuffer(const_cast<char*>(payload.c_str()), payload.length());
    msg.setFormat(MQFMT_STRING);

    if (queue.put(msg)) {
        std::cout << "Successfully sent message to " << queue.name() << std::endl;
    } else {
        std::cerr << "Failed to put message. Reason Code: "
                  << queue.reasonCode() << std::endl;
    }

    queue.close();
    manager.disconnect();

    return 0;
}
