#include "ndnClient.h"
#include "log.h"
#include <cassert>
#include <cstring>
#include <curl/curl.h>

// correct way to include ndn-cxx headers
// #include <ndn-cxx/face.hpp>
#include "face.hpp"

namespace Tangram {

/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2013-2016 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 *
 * @author Alexander Afanasyev <http://lasr.cs.ucla.edu/afanasyev/index.html>
 */


// Enclosing code in ndn simplifies coding (can also use `using namespace ndn`)
namespace ndn {
// Additional nested namespace could be used to prevent/limit name contentions
namespace examples {

class Consumer : noncopyable
{
public:
  void
  run()
  {
    Interest interest(Name("/example/testApp/randomData"));
    interest.setInterestLifetime(time::milliseconds(1000));
    interest.setMustBeFresh(true);

    m_face.expressInterest(interest,
                           bind(&Consumer::onData, this,  _1, _2),
                           bind(&Consumer::onNack, this, _1, _2),
                           bind(&Consumer::onTimeout, this, _1));

    std::cout << "Sending " << interest << std::endl;

    // processEvents will block until the requested data received or timeout occurs
    m_face.processEvents();
  }

private:
  void
  onData(const Interest& interest, const Data& data)
  {
    std::cout << data << std::endl;
  }

  void
  onNack(const Interest& interest, const lp::Nack& nack)
  {
    std::cout << "received Nack with reason " << nack.getReason()
              << " for interest " << interest << std::endl;
  }

  void
  onTimeout(const Interest& interest)
  {
    std::cout << "Timeout " << interest << std::endl;
  }

private:
  Face m_face;
};

} // namespace examples
} // namespace ndn


struct CurlGlobals {
    CurlGlobals() {
        LOGD("curl global init");
        curl_global_init(CURL_GLOBAL_ALL);
    }
    ~CurlGlobals() {
        LOGD("curl global shutdown");
        curl_global_cleanup();
    }
} s_curl;


NdnClient::Response getCanceledResponse() {
    UrlClient::Response response;
    response.canceled = true;
    return response;
}

NdnClient::NdnClient(Options options) : m_options(options) {
    assert(options.numberOfThreads > 0);
    // Start the curl threads.
    m_keepRunning = true;
    m_tasks.resize(options.numberOfThreads);
    for (uint32_t i = 0; i < options.numberOfThreads; i++) {
        m_threads.emplace_back(&UrlClient::curlLoop, this, i);
    }
}

NdnClient::~NdnClient() {
    // Make all tasks cancelled.
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        for (auto& request : m_requests) {
            if (request.callback) {
                auto response = getCanceledResponse();
                request.callback(std::move(response.data));
            }
        }
        m_requests.clear();
        for (auto& task : m_tasks) {
            task.response.canceled = true;
        }
    }
    // Stop the curl threads.
    m_keepRunning = false;
    m_requestCondition.notify_all();
    for (auto& thread : m_threads) {
        thread.join();
    }
}

bool NdnClient::addRequest(const std::string& url, UrlCallback onComplete) {
    // Create a new request.
    Request request = {url, onComplete};
    // Add the request to our list.
    {
        // Lock the mutex to prevent concurrent modification of the list by the curl loop thread.
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requests.push_back(request);
    }
    // Notify a thread to start the transfer.
    m_requestCondition.notify_one();
    return true;
}

void NdnClient::cancelRequest(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    // First check the pending request list.
    for (auto it = m_requests.begin(), end = m_requests.end(); it != end; ++it) {
        auto& request = *it;
        if (request.url == url) {
            // Found the request! Now run its callback and remove it.
            auto response = getCanceledResponse();
            if (request.callback) {
                request.callback(std::move(response.data));
            }
            m_requests.erase(it);
            return;
        }
    }
    // Next check the active request list.
    for (auto& task : m_tasks) {
        if (task.request.url == url) {
            task.response.canceled = true;
        }
    }
}

size_t curlWriteCallback(char* ptr, size_t size, size_t n, void* user) {
    // Writes data received by libCURL.
    auto* response = reinterpret_cast<UrlClient::Response*>(user);
    auto& buffer = response->data;
    auto addedSize = size * n;
    auto oldSize = buffer.size();
    buffer.resize(oldSize + addedSize);
    std::memcpy(buffer.data() + oldSize, ptr, addedSize);
    return addedSize;
}

int curlProgressCallback(void* user, double dltotal, double dlnow, double ultotal, double ulnow) {
    // Signals libCURL to abort the request if marked as canceled.
    auto* response = reinterpret_cast<UrlClient::Response*>(user);
    return static_cast<int>(response->canceled);
}

void NdnClient::curlLoop(uint32_t index) {
    assert(m_tasks.size() > index);
    Task& task = m_tasks[index];
    LOGD("curlLoop %u starting", index);
    // Create a buffer for curl error messages.
    char curlErrorString[CURL_ERROR_SIZE];
    // Set up an easy handle for reuse.
    auto handle = curl_easy_init();
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &curlWriteCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &task.response);
    curl_easy_setopt(handle, CURLOPT_PROGRESSFUNCTION, &curlProgressCallback);
    curl_easy_setopt(handle, CURLOPT_PROGRESSDATA, &task.response);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_HEADER, 0L);
    curl_easy_setopt(handle, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "gzip");
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, curlErrorString);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, m_options.connectionTimeoutMs);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, m_options.requestTimeoutMs);
    // Loop until the session is destroyed.
    while (m_keepRunning) {
        bool haveRequest = false;
        // Wait until the condition variable is notified.
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            if (m_requests.empty()) {
                LOGD("curlLoop %u waiting", index);
                m_requestCondition.wait(lock);
            }
            LOGD("curlLoop %u notified", index);
            // Try to get a request from the list.
            if (!m_requests.empty()) {
                // Take the first request from our list.
                task.request = m_requests.front();
                m_requests.erase(m_requests.begin());
                haveRequest = true;
            }
        }
        if (haveRequest) {
            // Configure the easy handle.
            const char* url = task.request.url.data();
            curl_easy_setopt(handle, CURLOPT_URL, url);
            LOGD("curlLoop %u starting request for url: %s", index, url);
            // Perform the request.
            auto result = curl_easy_perform(handle);
            // Get the result status code.
            long httpStatus = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpStatus);
            // Handle success or error.
            if (result == CURLE_OK && httpStatus >= 200 && httpStatus < 300) {
                LOGD("curlLoop %u succeeded with http status: %d for url: %s", index, httpStatus, url);
                task.response.successful = true;
            } else if (result == CURLE_ABORTED_BY_CALLBACK) {
                LOGD("curlLoop %u request aborted for url: %s", index, url);
                task.response.successful = false;
            } else {
                LOGE("curlLoop %u failed: '%s' with http status: %d for url: %s", index, curlErrorString, httpStatus, url);
                task.response.successful = false;
            }
            if (task.request.callback) {
                LOGD("curlLoop %u performing request callback", index);
                task.request.callback(std::move(task.response.data));
            }
        }
        // Reset the response.
        task.response.data.clear();
        task.response.canceled = false;
        task.response.successful = false;
    }
    LOGD("curlLoop %u exiting", index);
    // Clean up our easy handle.
    curl_easy_cleanup(handle);
}

} // namespace Tangram
