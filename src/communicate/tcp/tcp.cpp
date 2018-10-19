/*
Tencent is pleased to support the open source community by making
PhxPaxos available.
Copyright (C) 2016 THL A29 Limited, a Tencent company.
All rights reserved.

Licensed under the BSD 3-Clause License (the "License"); you may
not use this file except in compliance with the License. You may
obtain a copy of the License at

https://opensource.org/licenses/BSD-3-Clause

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" basis,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing
permissions and limitations under the License.

See the AUTHORS file for names of contributors.
*/

#include "tcp.h"
#include "phxpaxos/network.h"

namespace phxpaxos {

// TcpRead主要的事情都是交给EventLoop来处理了
// 在构造函数中这里需要将接收到的消息交给DefaultNetwork处理。
// TcpRead就是EventLoop的一个包装,本质上来说，也就是在初始化的时候
// 设置了一个20480的初始值。
TcpRead::TcpRead(NetWork * poNetWork)
  : m_oEventLoop(poNetWork) {
  // 这里是TcpRead，不需要用TcpClient发消息
  // 所以这里的EventLoop是没有TcpClient == nullptr
}

TcpRead::~TcpRead() {
}

int TcpRead::Init() {
  return m_oEventLoop.Init(20480);
}

void TcpRead::run() {
  m_oEventLoop.StartLoop();
}

void TcpRead::Stop() {
  m_oEventLoop.Stop();
  join();

  PLHead("TcpReadThread [END]");
}

EventLoop * TcpRead::GetEventLoop() {
  return &m_oEventLoop;
}

////////////////////////////////////////////////////////

TcpWrite::TcpWrite(NetWork * poNetWork)
  // 这里将自己的eventLoop指针给了TcpClient.
  // EventLoop里面会使用到pNetwork.
  : m_oTcpClient(&m_oEventLoop, poNetWork), m_oEventLoop(poNetWork) {
  // EventLoop中有一个TcpClient指针，
  // 如果设置了，那么在收到消息的时候，就会调用。
  // 如果没有设置，那么收到消息就不会调用TcpClient->DealWithWrite()
  m_oEventLoop.SetTcpClient(&m_oTcpClient);
}

TcpWrite::~TcpWrite() {
}

int TcpWrite::Init() {
  return m_oEventLoop.Init(20480);
}

void TcpWrite::run() {
  m_oEventLoop.StartLoop();
}

void TcpWrite::Stop() {
  m_oEventLoop.Stop();
  join();

  PLHead("TcpWriteThread [END]");
}

int TcpWrite::AddMessage(const std::string & sIP, const int iPort, const std::string & sMessage) {
  return m_oTcpClient.AddMessage(sIP, iPort, sMessage);
}

// EventLoop * TcpRead::GetEventLoop() 这里只有TcpRead有。但是TcpWrite没有。


////////////////////////////////////////////////////////

TcpIOThread::TcpIOThread(NetWork * poNetWork)
  : m_poNetWork(poNetWork) {
  m_bIsStarted = false;
  assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
  assert(signal(SIGALRM, SIG_IGN) != SIG_ERR);
  assert(signal(SIGCHLD, SIG_IGN) != SIG_ERR);

}

TcpIOThread::~TcpIOThread() {
  for (auto & poTcpRead : m_vecTcpRead) {
    delete poTcpRead;
  }

  for (auto & poTcpWrite : m_vecTcpWrite) {
    delete poTcpWrite;
  }
}

void TcpIOThread::Stop() {
  if (m_bIsStarted) {
    m_oTcpAcceptor.Stop();
    for (auto & poTcpRead : m_vecTcpRead) {
      poTcpRead->Stop();
    }

    for (auto & poTcpWrite : m_vecTcpWrite) {
      poTcpWrite->Stop();
    }
  }

  PLHead("TcpIOThread [END]");
}

int TcpIOThread::Init(const std::string & sListenIp,
                      const int iListenPort,
                      const int iIOThreadCount) {
  // iIOThreadCount指定了创建线程的数目
  // 无论是接收方，还是发送方，都需要有这么多线程。
  for (int i = 0; i < iIOThreadCount; i++) {
    // 生成一个TcpRead对象
    // TcpRead对象中会有一个EventLoop
    // EventLoop会有一个对m_poNetWork的引用
    TcpRead * poTcpRead = new TcpRead(m_poNetWork);
    assert(poTcpRead != nullptr);
    m_vecTcpRead.push_back(poTcpRead);
    // 把TcpRead对象中的EventLoop添加到
    // m_vecEventLoop这个vector中
    m_oTcpAcceptor.AddEventLoop(poTcpRead->GetEventLoop());

    // TcpWrite中也有一个EventLoop对象
    // m_poNetWork也是用来初始化EventLoop对象的。
    TcpWrite * poTcpWrite = new TcpWrite(m_poNetWork);
    assert(poTcpWrite != nullptr);
    m_vecTcpWrite.push_back(poTcpWrite);
  }
  
  // 这个函数经过层层包裹，实际上要完成的功能就是
  // 绑定并且开始监听TCP端口的经曲操作。
  m_oTcpAcceptor.Listen(sListenIp, iListenPort);
  int ret = -1;

  for (auto & poTcpRead : m_vecTcpRead) {
    // 每个线程都会创建epoll_fd
    ret = poTcpRead->Init();
    if (ret != 0) {
      return ret;
    }
  }

  for (auto & poTcpWrite: m_vecTcpWrite) {
    ret = poTcpWrite->Init();
    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

void TcpIOThread::Start() {
  m_oTcpAcceptor.start();
  for (auto & poTcpWrite : m_vecTcpWrite) {
    poTcpWrite->start();
  }

  for (auto & poTcpRead : m_vecTcpRead) {
    poTcpRead->start();
  }

  m_bIsStarted = true;
}

int TcpIOThread::AddMessage(const int iGroupIdx, const std::string & sIP, const int iPort, const std::string & sMessage) {
  int iIndex = iGroupIdx % (int)m_vecTcpWrite.size();
  return m_vecTcpWrite[iIndex]->AddMessage(sIP, iPort, sMessage);
}

}


