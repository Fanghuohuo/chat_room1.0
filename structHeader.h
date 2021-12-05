#ifndef FND_STRUCT_HEADER_H
#define FND_STRUCT_HEADER_H
#include <string>
//#include <boost/date_time/posix_time/posix_time.hpp>

//done!

struct Header{
    int bodySize;
    int type;
};

enum MessageType {
    MT_BIND_NAME = 1,
    MT_CHAT_INFO = 2,
    MT_ROOM_INFO = 3,
};

//这里假设双方都是c/c++写的，而且大小端都相同
//这里对齐也是非常小心的，采用四字节对齐
//这是客户端发的
struct BindName{
    char name[32];
    int nameLen;
};

//这是客户端发的
struct ChatInformation{
    char information[256];
    int infoLen;
};

//这是服务器发给客户端的
struct RoomInformation{
    BindName name;
    ChatInformation chat;
};

//这里是客户端的对标准内容输入的解析
bool parseMessage(const std::string& input, int* type, std::string& outbuffer);
#endif
