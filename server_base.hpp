//
// server_base.hpp
// web_server
// created by changkun at shiyanlou.com
//

#ifndef SERVER_BASE_HPP
#define    SERVER_BASE_HPP

#include <boost/asio.hpp>
#include <iostream>
#include <regex>
#include <unordered_map>
#include <thread>

namespace ShiyanlouWeb {

    typedef boost::asio::ip::tcp::socket HTTP;

    //对来自客户端的请求信息进行解析,为此设计了Request 结构体
    struct Request {
        // 请求方法, POST, GET; 请求路径; HTTP 版本
        std::string method, path, http_version;
        // 对 content 使用智能指针进行引用计数
        std::shared_ptr<std::istream> content;
        // 哈希容器, key-value 字典
        std::unordered_map<std::string, std::string> header;
        // 用正则表达式处理路径匹配
        std::smatch path_match;
    };

    // 使用 typedef 简化资源类型的表示方式
    typedef std::map<
        std::string, //用于存储请求路径的正则表达式 /match/123abc
        std::unordered_map< 
            std::string,    //用于存储请求方法  GET POST
            std::function<void(std::ostream&, Request&)>    //用于存储处理方法 
        >
    > resource_type;

    // socket_type 为 HTTP or HTTPS
    template <typename socket_type>
    class ServerBase {
    public:
        //resource_type 是一个 std::map，
        //其键为一个字符串，值则为一个无序容器std::unordered_map，
        //这个无序容器的键依然是一个字符串，其值接受一个返回类型为空、参数为 ostream 和 Request 的函数
        // 用于服务器访问资源处理方式
        //在handle.hpp文件就为resource定义了3种方法，分别是一个POST，两个GET
        resource_type resource;
        // 用于保存默认资源的处理方式
        //在handle.hpp文件就为default_resource定义了1种方法，是一个GET
        resource_type default_resource;

        // 构造服务器, 初始化端口, 默认使用一个线程
        ServerBase(unsigned short port, size_t num_threads=1) :
            //endpoint(boost::asio::ip::tcp::v4(), port),
        endpoint(boost::asio::ip::address::from_string("172.18.218.180"), port),
            acceptor(m_io_service, endpoint),
            num_threads(num_threads) {}

        void start();
    protected:
        //对于所有使用 Asio 的程序，都必须要包含至少一个 io_service 对象。
        //对于 Asio 这个 Boost 库而言，它抽象了诸如网络、串口通信等等这些概念，
        //并将其统一规为 IO 操作，所以 io_service 这个类提供了访问 I/O 的功能。
        // asio 库中的 io_service 是调度器，所有的异步 IO 事件都要通过它来分发处理
        // 换句话说, 需要 IO 的对象的构造函数，都需要传入一个 io_service 对象
        boost::asio::io_service m_io_service;
        //socket 是一个端到端的连接，所谓 endpoint 就是 socket 位于服务端的一个端点，
        //socket 是由 IP 地址和端口号组成的，那么当我们需要为其建立一个 IPv4 的网络，
        //首先可以建立一个 boost::asio::ip::tcp::endpoint 对象
        // IP 地址、端口号、协议版本构成一个 endpoint，并通过这个 endpoint 在服务端生成
        // tcp::acceptor 对象，并在指定端口上等待连接
        //有三种方式来让你建立一个端点：
        //endpoint()：这是默认构造函数，某些时候可以用来创建UDP/ICMP socket。
        //endpoint(protocol, port)：这个方法通常用来创建可以接受新连接的服务器端socket。
        //endpoint(addr, port):这个方法创建了一个连接到某个地址和端口的端点。
        boost::asio::ip::tcp::endpoint endpoint;
        //作为服务端，我们可能构建很多很多的连接从而响应并发，
        //所以当我们需要建立连接时候，就需要使用一个叫做 acceptor 的对象
        //acceptor用来接受客户端连接，创建虚拟的socket，异步等待客户端连接的对象
        // 所以，一个 acceptor 对象的构造都需要 io_service 和 endpoint 两个参数
        boost::asio::ip::tcp::acceptor acceptor;

        // 服务器线程池
        size_t num_threads;
        std::vector<std::thread> threads;

        // 所有的资源及默认资源都会在 vector 尾部添加, 并在 start() 中创建
        //存放map的迭代器
        std::vector<resource_type::iterator> all_resources;

        // 虚函数，需要不同类型的服务器实现这个方法
        virtual void accept() {}

        // 处理请求和应答
        void process_request_and_respond(std::shared_ptr<socket_type> socket) const;

        void respond(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request) const;
        Request parse_request(std::istream& stream) const;
    };        

    template <typename socket_type>
    void ServerBase<socket_type>::start() {
        // 默认资源放在 vector 的末尾, 用作默认应答
        // 默认的请求会在找不到匹配请求路径时，进行访问，故在最后添加
        for(auto it=resource.begin(); it!=resource.end();it++) {
            all_resources.push_back(it);
        }
        for(auto it=default_resource.begin(); it!=default_resource.end();it++) {
            all_resources.push_back(it);
        }

        // 调用 socket 的连接方式，还需要子类来实现 accept() 逻辑
        accept();

        // 如果 num_threads>1, 那么 m_io_service.run()
        // 将运行 (num_threads-1) 线程成为线程池
        for(size_t c=1;c<num_threads;c++) {
            threads.emplace_back(
                [this](){m_io_service.run();}
            );
        }
        // 主线程
        m_io_service.run();

        // 等待其他线程，如果有的话, 就等待这些线程的结束
        for(auto& t: threads)
            t.join();
    }

    // 处理请求和应答
    template <typename socket_type>
    void ServerBase<socket_type>::process_request_and_respond(std::shared_ptr<socket_type> socket) const {
        
        // 为 async_read_untile() 创建新的读缓存
        // shared_ptr 用于传递临时对象给匿名函数
        // 会被推导为 std::shared_ptr<boost::asio::streambuf>
        auto read_buffer = std::make_shared<boost::asio::streambuf>();

        //很多网络协议其实都是基于行实现的，也就是说这些协议元素是由 \r\n 符号进行界定，
        //HTTP 也不例外，所以在 Boost Asio 中，读取使用分隔符的协议，可以使用 async_read_untile() 方法
        //原型: boost::asio::async_read_until(socket, readbuffer, "\r\n\r\n", read_handler);
        boost::asio::async_read_until(
            *socket, 
            *read_buffer, 
            "\r\n\r\n", //以这个为终止符，看handler就能够明白
            //read_handler 是一个无返回类型的函数对象
            //它接受两个参数，一个是 boost::system::error_code，另一个是 size_t bytes_transferred
            //boost::system::error_code 用来描述操作是否成功，
            //而 size_t bytes_transferred 则是用来确定接受到的字节数
            [this, socket, read_buffer](const boost::system::error_code& ec, size_t bytes_transferred )
            {
                if(!ec) {   //操作成功
                    // 注意：read_buffer->size() 的大小不一定和 bytes_transferred 相等， Boost 的文档中指出：
                    // 在 async_read_until 操作成功后,  streambuf 在界定符之外可能包含一些额外的的数据
                    // 所以较好的做法是直接从流中提取并解析当前 read_buffer 左边的报头, 再拼接 async_read 后面的内容
                    size_t total = read_buffer->size();

                    // 转换到 istream
                    std::istream stream(read_buffer.get());

                    // 接下来要将 stream 中的请求信息进行解析，然后保存到 request 对象中           
                    // 被推导为 std::shared_ptr<Request> 类型
                    auto request = std::make_shared<Request>(); 

                    *request = parse_request(stream);

                    size_t num_additional_bytes = total-bytes_transferred;
                    std::cout << "num_additional_bytes:" << num_additional_bytes << std::endl;
                    if(request->header.count("Content-Length")>0){
                        std::cout<< "需要再拼接async_read的内容，大小为:" 
                            <<stoull(request->header["Content-Length"]) - num_additional_bytes 
                            << std::endl;
                    }
                    // 如果满足，同样读取
                    if(request->header.count("Content-Length")>0) { //出现 Content-Length 这个字段表示是POST请求，并且有发送内容过来
                        boost::asio::async_read(
                            *socket, 
                            *read_buffer,
                            boost::asio::transfer_exactly(stoull(request->header["Content-Length"]) - num_additional_bytes ),    //返回0表示结束read，否则表示下一次要读取的字符数
                            [this, socket, read_buffer, request](const boost::system::error_code& ec, size_t bytes_transferred) {
                                if(!ec) {
                                    // 将指针作为 istream 对象存储到 read_buffer 中
                                    request->content = std::shared_ptr<std::istream>(new std::istream(read_buffer.get()));
                                    respond(socket, request);
                                }
                            }
                        );
                    } else {
                        //没有 Content-Length 这个字段
                        //std::cout<<"Content-Length is not exist" <<std::endl;
                        respond(socket, request);
                    }
                }
            });
    }

    // 解析请求
    template<typename socket_type>
    Request ServerBase<socket_type>::parse_request(std::istream& stream) const {
        Request request;
        // 使用正则表达式对请求报头进行解析，通过下面的正则表达式
        // 可以解析出请求方法(GET/POST)、请求路径以及 HTTP 版本
        //经验证 验证的字符串的样式为：GET /match/sdsd123 HTTP/1.1
        std::regex e("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
        std::smatch sub_match;

        //从第一行中解析请求方法、路径和 HTTP 版本
        std::string line;
        getline(stream, line);
        std::cout<<"\nparse_request:"<<std::endl;
        std::cout<<line<<std::endl;
        line.pop_back();
        if(std::regex_match(line, sub_match, e)) {
            request.method       = sub_match[1];    // GET or POST
            request.path         = sub_match[2];    // /match/abc123
            request.http_version = sub_match[3];    // HTTP/1.1

            // 解析头部的其他信息
            bool matched;
            e="^([^:]*): ?(.*)$";
            do {
                getline(stream, line);
                std::cout<<line<<std::endl;
                line.pop_back();
                //匹配：
                //Host: localhost:12345
                //User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:31.0) Gecko/20100101 Firefox/31.0
                //Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
                //Accept-Language: en-US,en;q=0.5
                //Accept-Encoding: gzip, deflate
                //Connection: keep-alive
                //Cache-Control: max-age=0
                matched=std::regex_match(line, sub_match, e);   
                if(matched) {
                    request.header[sub_match[1]] = sub_match[2];
                }
            } while(matched==true);
        }
        return request;
    }

    // 应答
    // 解析请求
    template<typename socket_type>
    void ServerBase<socket_type>::respond(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request) const {
        // 对请求路径和方法进行匹配查找，并生成响应
        for(auto res_it: all_resources) {
            std::regex e(res_it->first);    //请求路径的正则
            std::smatch sm_res;

            if(std::regex_match(request->path, sm_res, e)) {
                if(res_it->second.count(request->method)>0) {
                    std::cout<<"正则表达式匹配:"<<res_it->first << " <===>" << request->path <<std::endl; 
                    request->path_match = move(sm_res);

                    // 会被推导为 std::shared_ptr<boost::asio::streambuf>
                    auto write_buffer = std::make_shared<boost::asio::streambuf>();
                    std::ostream response(write_buffer.get());  //get() 返回智能指针保存的指针
                    res_it->second[request->method](response, *request);

                    // 在 lambda 中捕获 write_buffer 使其不会再 async_write 完成前被销毁
                    boost::asio::async_write(
                        *socket, 
                        *write_buffer,
                        [this, socket, request, write_buffer](
                            const boost::system::error_code& ec, 
                            size_t bytes_transferred) 
                        {
                            // HTTP 持久连接(HTTP 1.1), 递归调用
                            if(!ec && stof(request->http_version)>1.05)
                                process_request_and_respond(socket);
                        }
                    );
                    return;
                }
            }
        }
    }


    template<typename socket_type>
    class Server : public ServerBase<socket_type> {};  
}
#endif    /* SERVER_BASE_HPP */