﻿/*
Copyright (c) 2013, Tolga HOŞGÖR
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the FreeBSD Project.
*/

#include <algorithm>

#include "threadpool11/Pool.h"

namespace threadpool11
{

Pool::Pool(WorkerCountType const& workerCount)
{
  spawnWorkers(workerCount);
}

Pool::~Pool()
{
  joinAll();
}

void Pool::joinAll()
{
  do
  {
    size_t nInactiveWorkers;
    size_t nActiveWorkers;

    std::lock(activeWorkerContMutex, inactiveWorkerContMutex);

    nInactiveWorkers = inactiveWorkers.size();
    nActiveWorkers = activeWorkers.size();

    auto activeWorkIt = --activeWorkers.end();
    auto inactiveWorkIt = --inactiveWorkers.end();

    activeWorkerContMutex.unlock();
    inactiveWorkerContMutex.unlock();

    if(nInactiveWorkers == 0 && nActiveWorkers == 0)
      break;

    if(nInactiveWorkers > 0)
    {
      inactiveWorkIt->terminate = 1;
      std::unique_lock<std::mutex> activateLock(inactiveWorkIt->activatorMutex);
      inactiveWorkIt->isWorkReallyPosted = true;
      inactiveWorkIt->activator.notify_one();
      activateLock.unlock();
      inactiveWorkIt->thread.join();
      std::lock_guard<std::mutex> contLock(inactiveWorkerContMutex);
      inactiveWorkers.erase(inactiveWorkIt);
    }

    if(nActiveWorkers > 0)
    {
      activeWorkIt->terminate = 1;
      std::unique_lock<std::mutex> activateLock(activeWorkIt->activatorMutex);
      activeWorkIt->isWorkReallyPosted = true;
      activeWorkIt->activator.notify_one();
      activateLock.unlock();
      activeWorkIt->thread.join();
      std::lock_guard<std::mutex> contLock(activeWorkerContMutex);
      activeWorkers.erase(activeWorkIt);
    }
  } while(true);
}

Pool::WorkerCountType Pool::getWorkQueueCount() const
{
  std::lock_guard<std::mutex> l(enqueuedWorkMutex);
  return enqueuedWork.size();
}

Pool::WorkerCountType Pool::getActiveWorkerCount() const
{
  std::lock_guard<std::mutex> l(activeWorkerContMutex);
  return activeWorkers.size();
}

Pool::WorkerCountType Pool::getInactiveWorkerCount() const
{
  std::lock_guard<std::mutex> l(inactiveWorkerContMutex);
  return inactiveWorkers.size();
}

void Pool::increaseWorkerCountBy(WorkerCountType const& n)
{
  std::lock_guard<std::mutex> l(inactiveWorkerContMutex);
  spawnWorkers(n);
}

Pool::WorkerCountType Pool::decreaseWorkerCountBy(WorkerCountType n)
{
  std::lock_guard<std::mutex> l(inactiveWorkerContMutex);
  n = std::min(n, static_cast<Pool::WorkerCountType>(inactiveWorkers.size()));
  for(WorkerCountType i = 0; i < n; ++i)
  {
    auto last = --inactiveWorkers.end();
    last->terminate = true;
    last->activatorMutex.lock();
    last->isWorkReallyPosted = true;
    last->activator.notify_one();
    last->activatorMutex.unlock();
    last->thread.join();
    inactiveWorkers.pop_back();
  }
  return n;
}

void Pool::spawnWorkers(WorkerCountType n)
{
  //'OR' makes sure the case where one of the expressions is zero, is valid.
  assert(static_cast<WorkerCountType>(inactiveWorkers.size() + n) > n || static_cast<WorkerCountType>(inactiveWorkers.size() + n) > inactiveWorkers.size());
  while(n-- > 0)
  {
    auto workerIterator = inactiveWorkers.emplace(inactiveWorkers.end(), this);
    workerIterator->iterator = workerIterator;
    std::unique_lock<std::mutex> lock(workerIterator->initMutex);
    if(workerIterator->isReallyInitialized == 0)
      workerIterator->initializer.wait(lock, [&](){ return workerIterator->isReallyInitialized; });
  }
}

}
