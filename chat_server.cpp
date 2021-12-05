#include "chat_message.hpp"
#include "structHeader.h"

#include <boost/asio.hpp>

#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>

#include <cstdlib>

using boost::asio::ip::tcp;

//----------------------------------------------------------------------

//这里不用vector，
//1 vector对首位删除慢
//2 可能会迭代器失效
//3 会有vector扩容的问题
typedef std::deque<chat_message> chat_message_queue;

//----------------------------------------------------------------------

//服务器端主要完成两个部分：
//1 服务器端的主逻辑
//2 围绕消息协议编程,比如说增加了新的协议（新的struct里面的内容）
//这里的协议就是chat_message

//前项声明
class chat_session;
//using不用等号就会出现expected nested-name-specifier错误
//shared_ptr头文件是<memory>
using chat_session_ptr = std::shared_ptr<chat_session>;
//class chat_room; //这里不能这么写，因为这样写是不能生成实例的
//前项声明会有问题

//这里要把声明搞完整
class chat_room {
public:
   //这里不能写具体的名字
   void join(chat_session_ptr);
   void leave(chat_session_ptr);
   void deliver(const chat_message&);
private:
   std::set<chat_session_ptr> sessions_;
   enum { max_recent_msgs = 100 };
   chat_message_queue recent_msgs_;
};     

//----------------------------------------------------------------------

//客户端连接进来作为一个session（事件）
//public std::enable_shared_from_this<chat_session>派生出来
//意思是用智能指针去管理
class chat_session : public std::enable_shared_from_this<chat_session>{
public:
  chat_session(tcp::socket socket, chat_room& room)
    : socket_(std::move(socket)),
      room_(room){
  }
  
  void start(){
    //这个shared_from_this()返回的是这个类本身的一个shared_ptr
    //shared_ptr<chat_session>()
    room_.join(shared_from_this());
    //这里其实已经成功连接进来了，之后就是接受服务器的消息了
    do_read_header(); //读报文头部
  }

  void deliver(const chat_message& msg){
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    //第一次为空，只有为空的时候才会调用do_write
    //这里是防止调用两次do write
    if (!write_in_progress){
      do_write();
    }
  }

  std::string getName() { return m_name; }

private:
  //这种函数要封装起来，这样以后就可以复用的，只需要修改接口就行了
  //RoomInformation这里是把数据都封装成RoomInformation格式
  RoomInformation buildRoomInfo() const {
    //把之前绑定好的名字和发过来的消息，封装成一个roominformation
    /*struct RoomInformation{
        BindName name;
        ChatInformation chat;
    };*/
    BindName name;
    name.nameLen = m_name.size();
    std::memcpy(name.name, m_name.data(), m_name.size());

    ChatInformation chat;
    chat.infoLen = m_chatInformation.size();
    std::memcpy(chat.information, m_chatInformation.data(), 
        m_chatInformation.size());

    return RoomInformation{name,chat};
  }

  //handleMessage也是一样，把脏活封装起来
  void handleMessage(){
    if(read_msg_.type() == MT_BIND_NAME) {
        const BindName* bind = reinterpret_cast<const BindName*>(read_msg_.body());
        m_name.assign(bind->name, bind->name + bind->nameLen);
    }else if(read_msg_.type() == MT_CHAT_INFO) {
        const ChatInformation* chat = reinterpret_cast<const ChatInformation*>(read_msg_.body());
        m_chatInformation.assign(chat->information, chat->information + chat->infoLen);
        auto rinfo = buildRoomInfo();
        //封装成RoomInformation
        chat_message msg;
        msg.setMessage(MT_ROOM_INFO,&rinfo,sizeof(rinfo));
        room_.deliver(msg);
    }else{
       //啥都不做 
    }
  }

  void do_read_header(){
    //这里为了不被析构，所以搞了个这个内容
    auto self(shared_from_this());
    //之后异步的去读
    boost::asio::async_read(socket_,
        //把头四个字节读到buff里面去
        boost::asio::buffer(read_msg_.data(), chat_message::header_length),
        //第三个参数是一个函数指针，也就是一个回调函数
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        { //ec是error_code也就是模块或者系统错误，而且头部信息合法
          //body长度小于512
          if (!ec && read_msg_.decode_header()){
            do_read_body();
          }
          else
          { //出错就断开，这里智能指针引用计数为0
            room_.leave(shared_from_this());
          }
        });
  }

  //在read header里面已经获得了body的长度（肯定小于512字节）
  //之后在此基础上进行分析即可
  void do_read_body(){
    //这里的目的和上面一样
    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        //也是一样，把body的内容读到buff里面，错位了四个字节
        boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec){
            //handleMessage负责处理body里面的内容，处理完以后继续异步读header
            handleMessage();
            do_read_header();
          }
          else{
            room_.leave(shared_from_this());
          }
        });
  }

  //写write_msgs_里面的信息，相当于把chat_message消息都发出去
  void do_write(){
    auto self(shared_from_this());
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec)
          { //头部信息写完了，就检查是不是空的
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            { //继续写
              do_write();
            }
          }
          else{
            room_.leave(shared_from_this());
          }
        });
  }
  
  tcp::socket socket_;
  chat_room& room_; //这里肯定是room的生命周期更长
  std::string m_name;  //这里是这个session的名字
  std::string m_chatInformation;  
  chat_message read_msg_;
  chat_message_queue write_msgs_;
};

//----------------------------------------------------------------------

//chat_room函数实现
void chat_room::join(chat_session_ptr session)
{
  sessions_.insert(session);
  std::cout << "one client join the room" << std::endl;
  for (const auto& msg: recent_msgs_)
    session->deliver(msg);
}

void chat_room::leave(chat_session_ptr session){
  std::cout << "one client ";
  std::string name = session->getName();
  if(name.size() == 0)
    std::cout << "no name ";
  else
    std::cout << name << " ";
  std::cout << "gone!" << std::endl;
  sessions_.erase(session);
}

void chat_room::deliver(const chat_message& msg){
  //把消息push到接受队列最后
  recent_msgs_.push_back(msg);
  //消息超过一定长度就扔掉
  while (recent_msgs_.size() > max_recent_msgs)
    recent_msgs_.pop_front();
  //智能指针拷贝是普通指针拷贝的10倍
  //调用chat_sesstion的deliver
  for (auto& session: sessions_)
    session->deliver(msg);
}

//----------------------------------------------------------------------

class chat_server{
public:
  chat_server(boost::asio::io_context& io_context,
      const tcp::endpoint& endpoint)
    : acceptor_(io_context, endpoint){
    do_accept();
  }

private:
  void do_accept(){
    //这里异步连接一个新的客户端
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket){
          if (!ec){
            auto session = std::make_shared<chat_session>(std::move(socket), room_);
            session->start();
          }
          //这里可能会有错误，但是服务器端的工作不能停
          //比如三次握手失败了，失败的逻辑在客户端那边处理，服务器不管，继续监听
          do_accept();
        });
  }

  //acceptor就是那个监听器
  tcp::acceptor acceptor_;
  chat_room room_; //这里为什么不用引用？因为可能绑定多个端口有多个room
};

//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    { //每一个chat server就是一个room，这里可以绑定多个端口
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";
      return 1;
    }
    
    boost::asio::io_context io_context;

    std::list<chat_server> servers;
    for (int i = 1; i < argc; ++i)
    { //这里就是在绑定端口，进行监听
      tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
      servers.emplace_back(io_context, endpoint);
    }
    //这里是异步的，只要server还有服务就不会退出
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
