本项目主要调用muduo库+mysql数据库管理客户信息实现聊天室项目的服务端，以及一个不带ui界面的简易客户端用于测试。主要练习规范的分文件编程，学习muduo的使用方法。

1.涉及到的知识点：
1.防止sql注入：使用预编译模板：大概用法是这样的，原理我也不是很懂，大概是如果采用拼接的话，数据库对整句做解析，分辨不出来哪里是用户填的哪里是开发者预先写好的，所以用户可以通过恶意语句进行绕过，而预编译的话就避免了这个问题。
<img width="415" height="313" alt="image" src="https://github.com/user-attachments/assets/fae667d2-e874-43bc-b1a9-14be6f5c1c0a" />

2.muduo：
TcpServer ：
setThreadNum(): 设置子线程（SubReactor）的数量。
start(): 启动底层的监听。
setConnectionCallback(): 当连接建立或断开时触发。
setMessageCallback(): 当收到客户端数据时触发。
setWriteCompleteCallback(): 数据完全发送给客户端后触发（用于流量控制）
Muduo的设计模式是一个监听主线程+若干工作从线程。
setThreadNum(): 设置的就是工作从线程的数量。
具体如何处理工作，Muduo为我们预留了两个函数：一个是setConnectionCallback()，参数是void(const TcpConnectionPtr&); 我们需要写一个形如此的函数作为参数传入setConnectionCallback()。当监听到新连接时，或客户连接断开时，muduo的Server会回调这个函数（void(const TcpConnectionPtr&);），你需要在这里完成你希望接收到连接或连接关闭时的处理，而为了写这个，我们得知道muduo的服务器为我们写了哪些操作（即我们不需要完成什么）。Muduo的服务器在接收到连接时，会将其注册到监听表，而在连接关闭时，会将其从监听表里踢出去。
另一个是setMessageCallback(): 它的参数是：void(const TcpConnectionPtr&, Buffer*, Timestamp);同理，我们需要写一个形如此的函数，然后将其作为参数传进去。这个函数会在收到新消息的时候被触发。收到消息要做什么处理，就取决于你的业务逻辑了。
若业务是轻量的，简单的写一个解析+工作函数就好了。若业务是复杂的，你希望从线程不要阻塞在这里，想开额外的工作线程去做，也是可行的。另外，虽然从底层逻辑上来说我们的从线程被设计为一个IO线程，也就是send工作实际上还是从线程在做，但是从封装上边实际上是解耦掉了的，如果按照工作线程去写，我们需要在工作线程里调用TcpConnection->send();(看起来就就像是工作线程在回消息——但是不是的，我会在下文聊TcpConnection的时候聊到这一点）
虽然本项目没有实现线程池，但在思路上是简易的，大概是：弄一线程池，然后弄一任务队列，线程池启动的时候，建立若干个线程去跑，检查任务队列里有没有内容，没有就cv.wait()，释放锁睡觉。从线程函数负责解析后，把对应的工作函数handler取出来，然后把参数打包，打成一个包扔到任务队列里边去，notify一下，线程池的线程起来抢包。

：大概就长这样。
<img width="416" height="53" alt="image" src="https://github.com/user-attachments/assets/d371960f-acd5-4502-86a8-74a2231d0ecc" />








1.TcpConnection：客户的连接信息，就像客户的档案，整体是对event的抽象和封装
成员：socket_,channel_,loop_,inputBuffer_,outputBuffer,state,context_
Socket_：对socket的封装（就是对接这个客户的套接字）
Channel_:客户的socket编号，触发方式（可读？可写？），实际触发的信号
Loop_:负责处理这个客户连接的从线程的信息，具体如下：
<img width="416" height="94" alt="image" src="https://github.com/user-attachments/assets/9727253f-c2d7-48aa-8ceb-7a19363c1206" />

inputBuffer_：读区
outputBuffer：写区
State：状态，用于管理shutdown逻辑
Context：开发者用客户信息便签（这是一个any类对象），可以挂载任意类型对象
函数：
会被开发者使用到的主要就两个：
一个是send(string/Buffer)：这里的实现很有意思，它会检查当前线程是否是负责监听该客户请求的那个从线程，如果不是（也就是你单独开了个工作线程在处理逻辑），它会把负责发送逻辑的函数传给对应从线程中的一个函数，并唤醒从线程（也就是往这个从线程的wakeupFd_里写一个数据），从线程检查发现是由于发送逻辑被唤醒的，接着去执行这个发送逻辑函数。（这里有一些难读的套娃，但大致来说就是如此）。
正如前文所说，我们希望将收发工作都集中在从线程中完成，原因是如果直接在工作线程中对客户的套接字write，与从线程对客户的操作会产生冲突，产生竞态条件，这样就需要加锁，锁是笨重的，为了提高效率，所以我们干脆将收发的逻辑全部包含在从线程中完成。
一个是shutdown()：这里的设计也很精妙，如上文所说我们会按照同样的方式来保证关闭连接的逻辑是在从线程中执行的。同时这个关闭是柔和的，如果我们仍有没有发出去的消息，是不会执行任何销毁的操作的，只对状态进行更改，直接return。负责处理写的函数在写完后会检查状态，如果是kDisconnecting，则再次调用shutdown（）。


<img width="416" height="130" alt="image" src="https://github.com/user-attachments/assets/65faa017-771b-455b-a92d-ae28ac1d1ab1" />













（也聊聊epoll吧，以防忘记：
. Muduo 的策略：只有“发不完”才关注
Muduo 绝不会在连接建立后就默认关注 EPOLLOUT。它的策略是：默认只管读，要写时才看情况管写。
场景 A：数据一次性发完了
当你调 send 时，如果 sendInLoop 直接通过 write 把数据全塞进了内核，它就执行完了。
结果：它压根不会去开启 EPOLLOUT 监听。
场景 B：内核满了，剩下一点没发完
这时 sendInLoop 发现还有 50KB 没发出去。
它把这 50KB 存入 outputBuffer_。
它调用 channel_->enableWriting()。这才会真正去 epoll_ctl 注册 EPOLLOUT。
结果：此时 epoll_wait 会返回，因为缓冲区有空位。

3. “用完即关”：关键的 handleWrite
一旦 EPOLLOUT 触发，从线程会跳进 TcpConnection::handleWrite：
它把 outputBuffer_ 里的数据拿出来 write。
重点来了：如果数据全部发完了，它会立刻执行：
C++
channel_->disableWriting(); // 也就是 epoll_ctl(fd, MOD, events & ~EPOLLOUT)
结果：一旦缓冲区清空，它就关掉“可写”监听。epoll_wait 恢复平静，继续阻塞等待。
