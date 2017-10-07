//
// server_http.hpp
// web_server
// created by changkun at shiyanlou.com
//

#ifndef SERVER_HTTP_HPP
#define SERVER_HTTP_HPP

#include "server_base.hpp"

namespace ShiyanlouWeb {
    template<>
    class Server<HTTP> : public ServerBase<HTTP> {
    public:
        // 通过端口号、线程数来构造 Web 服务器, HTTP 服务器比较简单，不需要做相关配置文件的初始化
        Server(unsigned short port, size_t num_threads=1) :
            ServerBase<HTTP>::ServerBase(port, num_threads) {};
    private:
        // 实现 accept() 方法
        void accept() {
            // 为当前连接创建一个新的 socket
            // Shared_ptr 用于传递临时对象给匿名函数
            // socket 会被推导为 std::shared_ptr<HTTP> 类型
            auto socket = std::make_shared<HTTP>(m_io_service);

            acceptor.async_accept(
                *socket, 
                [this, socket](const boost::system::error_code& ec) {
                    // 如果出现错误
                    if(ec){
                        cout<<"ec!=0 ,error"<<endl;
                        return;
                    } 
                    //ec=0，说明没有出错
                    process_request_and_respond(socket);

                    // 立即启动并接受一个连接,用来创建另外一个“等待客户端连接”的异步操作，
                    //从而使service.run()循环一直保持忙碌状态
                    accept();
                }
            );
        }
    };
}
#endif    /* SERVER_HTTP_HPP */