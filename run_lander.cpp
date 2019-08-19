/**
 * This is log lander application of WTLOG System.
 * Compile this and run the executable file to make your machine a lander.
 * Reference the ip and listen port. Default is 127.0.0.1:8089.
 * Author: LiWentan.
 * Date: 2019/7/19.
 */
 
#include "wtloglander.h"

using namespace std;

int main(int argc, char** argv) {
    // Read port.
    short port;
    string ip;
    if (argc <= 2) {
        port = 8089;
        ip = "127.0.0.1";
        cout << "Use default socket: 127.0.0.1:8089." << endl;
    } else {
        port = wttool::str2num(argv[2]);
        ip = argv[1];
        cout << "Read socket from parameter: " << ip << ":" << wttool::num2str(port) << "." << endl;
    }
    
    // Construct and start the lander.
    wtlog::WTLogLander lad = wtlog::WTLogLander("./");
    if (lad.connect(ip, port) == false) {
        cout << "Start lander failed, try again.\n";
        return 0;
    }
    
    // Listen the command.
    char comm[32];
    while (true) {
        cin >> comm;
        if (strcmp(comm, "stop") == 0) {
            lad.disconnect();
            continue;
        } else if (strcmp(comm, "stat")) {
            
        }
    }
    
    return 0;
}
