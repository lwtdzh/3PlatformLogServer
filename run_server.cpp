/**
 * This is log server application of WTLOG System.
 * Compile this and run the executable file to make your machine a server.
 * Reference the listen port. Default is 8089.
 * Author: LiWentan.
 * Date: 2019/7/19.
 */

#include "wtlogserver.h"

using namespace std;

int main(int argc, char** argv) {
    // Read port.
    short port;
    if (argc <= 1) {
        port = 8089;
        cout << "Use default port: " << port << "." << endl;
    } else {
        port = wttool::str2num(argv[1]);
        cout << "Read port from parameter: " << port << "." << endl;
    }
    
    // Construct and start the server.
    wtlog::WTLogServer svr = wtlog::WTLogServer();
    if (svr.start() == false) {
        cout << "Start server failed, try again.\n";
        return 0;
    }
    
    // Listen the command.
    char comm[32];
    while (true) {
        cin >> comm;
        if (strcmp(comm, "stop")) {
            svr.stop();
            continue;
        } else if (strcmp(comm, "stat")) {
            wtlog::StatInfo stat_inf = svr.status();
            stringstream ss;
            ss << "Following is the connected opposite: \n";
            ss << "Clients: \n";
            for (size_t i = 0; i < stat_inf.client_socket.size(); ++i) {
                ss << "i: " << stat_inf.client_socket[i] << ".\n";
            }
            ss << "Landers: \n";
            for (size_t i = 0; i < stat_inf.lander_socket.size(); ++i) {
                ss << "i: " << stat_inf.lander_socket[i] << ".\n";
            }
            cout << ss.str() << "\n\n";
            continue;
        }
    }
    
    return 0;
}
