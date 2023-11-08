// #pragma once
// #include "macoro/task.h"
// #include "coproto/Socket/BufferingSocket.h"
// #include "secure-join/Defines.h"

// macoro::task<> validateCols(std::string& visaJoinCols, std::string& clientJoinCols, 
//                             std::string& selectVisaCols, std::string& selectClientCols, 
//                             coproto::Socket& sock, bool isSender)
// {
//     MC_BEGIN(macoro::task<>, &visaJoinCols, &clientJoinCols, &selectVisaCols, 
//                 &selectClientCols, &sock, &isSender,
//                 tvisaJoinCols = std::string(),
//                 tclientJoinCols = std::string(),
//                 tselectVisaCols = std::string(),
//                 tselectClientCols = std::string()
//     );

//     if(isSender)
//     {
//         if(visaJoinCols.size())
//             MC_AWAIT(sock.send(visaJoinCols));
//         if(clientJoinCols.size())
//             MC_AWAIT(sock.send(clientJoinCols));
//         if(selectVisaCols.size())
//             MC_AWAIT(sock.send(selectVisaCols));    
//         if(selectClientCols.size())
//             MC_AWAIT(sock.send(selectClientCols));        
//     }
//     else
//     {
//         tvisaJoinCols.resize(visaJoinCols.size());
//         tclientJoinCols.resize(clientJoinCols.size());
//         tselectVisaCols.resize(selectVisaCols.size());
//         tselectClientCols.resize(selectClientCols.size());

//         if(visaJoinCols.size())
//             MC_AWAIT(sock.recv(tvisaJoinCols));
        
//         if(clientJoinCols.size()) 
//             MC_AWAIT(sock.recv(tclientJoinCols));
        
//         if(selectVisaCols.size())
//             MC_AWAIT(sock.recv(tselectVisaCols));
        
//         if(selectClientCols.size())
//             MC_AWAIT(sock.recv(tselectClientCols));

//         if( visaJoinCols.compare(tvisaJoinCols) != 0 ||
//             clientJoinCols.compare(tclientJoinCols) != 0 ||
//             selectVisaCols.compare(tselectVisaCols) != 0 ||
//             selectClientCols.compare(tselectClientCols) != 0)
//         {
//             std::string temp = "Both are parties are giving different cols as input\n";
//                 // + LOCATION;
//             throw std::runtime_error(temp);
//         }
        
//     }

//     MC_END();
// }