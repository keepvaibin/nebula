// SPDX-License-Identifier: GPL-3.0-only
// The update check's HTTP on Apple's systems: NSURLSession (the system's TLS, certificate trust and proxy settings).
// Each call runs one task and waits for it on the calling thread (always a thread of the update core's own).
#include "update_internal.h"

#import <CFNetwork/CFNetwork.h>
#import <Foundation/Foundation.h>

#include <cstdio>
#include <unistd.h>

using galaxy::update::detail::Transport;

// Streams a task's body into a string or a file, within a byte limit, and signals when the task ends.
@interface NebulaUpdateReceiver : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) std::string* text;
@property(nonatomic) std::FILE* file;
@property(nonatomic) std::uint64_t written;
@property(nonatomic) std::uint64_t limit;
@property(nonatomic) BOOL over;
@property(nonatomic) BOOL failed;
@property(nonatomic) BOOL restarted;   // a 200 answer to a range request: the file starts again
@property(nonatomic) NSInteger status;
@property(nonatomic) const std::atomic_bool* cancel;
@property(nonatomic) const std::function<void(std::uint64_t)>* progress;
@property(nonatomic, strong) dispatch_semaphore_t done;
@end

@implementation NebulaUpdateReceiver
- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completion {
    (void)session;
    (void)task;
    NSHTTPURLResponse* http = [response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse*)response : nil;
    self.status = http != nil ? http.statusCode : 0;
    if (self.file != nullptr && self.status == 200 && self.written > 0) {
        // The server sent the whole file instead of the rest.
        std::fflush(self.file);
        if (std::fseek(self.file, 0, SEEK_SET) != 0 || ftruncate(fileno(self.file), 0) != 0) {
            self.failed = YES;
            completion(NSURLSessionResponseCancel);
            return;
        }
        self.written = 0;
        self.restarted = YES;
    }
    completion(self.status >= 200 && self.status < 300 ? NSURLSessionResponseAllow : NSURLSessionResponseCancel);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
    (void)session;
    if ((self.cancel != nullptr && self.cancel->load(std::memory_order_relaxed)) || self.over || self.failed) {
        [task cancel];
        return;
    }
    if (self.written + data.length > self.limit) {
        self.over = YES;
        [task cancel];
        return;
    }
    __block BOOL ok = YES;
    [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL* stop) {
        if (self.text != nullptr) self.text->append(static_cast<const char*>(bytes), range.length);
        if (self.file != nullptr && std::fwrite(bytes, 1, range.length, self.file) != range.length) {
            ok = NO;
            *stop = YES;
        }
    }];
    if (!ok) {
        self.failed = YES;
        [task cancel];
        return;
    }
    self.written += data.length;
    if (self.progress != nullptr && *self.progress) (*self.progress)(self.written);
}

- (void)URLSession:(NSURLSession*)session
                 task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
            newRequest:(NSURLRequest*)request
     completionHandler:(void (^)(NSURLRequest*))completion {
    (void)session;
    (void)task;
    (void)response;
    // A redirect never leaves HTTPS (GitHub's assets redirect to its storage over HTTPS).
    completion([request.URL.scheme isEqualToString:@"https"] ? request : nil);
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    (void)session;
    (void)task;
    if (error != nil) self.failed = YES;
    dispatch_semaphore_signal(self.done);
}
@end

namespace galaxy::update::detail {

namespace {

int run(NSString* address, NebulaUpdateReceiver* receiver, std::uint64_t resume_from, int timeout_ms, bool download) {
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:address];
        if (url == nil) return -1;
        NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = download ? 30.0 : timeout_ms / 1000.0;  // a download: until it stalls 30 s
        if (!download) configuration.timeoutIntervalForResource = timeout_ms / 1000.0;
        configuration.HTTPAdditionalHeaders = @{@"User-Agent" : @"Nebula", @"Accept" : @"application/vnd.github+json"};
        receiver.done = dispatch_semaphore_create(0);
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration delegate:receiver delegateQueue:nil];
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        if (resume_from > 0) {
            [request setValue:[NSString stringWithFormat:@"bytes=%llu-", static_cast<unsigned long long>(resume_from)]
                forHTTPHeaderField:@"Range"];
        }
        NSURLSessionDataTask* task = [session dataTaskWithRequest:request];
        [task resume];
        dispatch_semaphore_wait(receiver.done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
        if (receiver.over) return -2;
        if (receiver.status == 0) return -1;
        if (receiver.failed && receiver.status >= 200 && receiver.status < 300) return -1;  // cut off mid-body
        return static_cast<int>(receiver.status);
    }
}

// An enable key of the system's proxy settings by its value: the dictionary holds the key with 0 when the proxy is off.
bool turned_on(CFDictionaryRef settings, CFStringRef key) {
    const void* value = CFDictionaryGetValue(settings, key);
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) return false;
    int on = 0;
    return CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &on) && on != 0;
}

// A proxy the HTTPS request may go through: then only the request can tell whether the server is reachable.
bool proxied() {
    CFDictionaryRef settings = CFNetworkCopySystemProxySettings();
    if (settings == nullptr) return false;
#if TARGET_OS_OSX
    const bool on = turned_on(settings, kCFNetworkProxiesHTTPSEnable) || turned_on(settings, kCFNetworkProxiesSOCKSEnable) ||
                    turned_on(settings, kCFNetworkProxiesProxyAutoConfigEnable) ||
                    turned_on(settings, kCFNetworkProxiesProxyAutoDiscoveryEnable);
#else
    // iOS has one proxy for HTTP and HTTPS, and automatic configuration.
    const bool on = turned_on(settings, kCFNetworkProxiesHTTPEnable) || turned_on(settings, kCFNetworkProxiesProxyAutoConfigEnable);
#endif
    CFRelease(settings);
    return on;
}

class AppleTransport final : public Transport {
public:
    int get(const std::string& url, std::string& body, std::size_t max_bytes, int timeout_ms) override {
        body.clear();
        NebulaUpdateReceiver* receiver = [[NebulaUpdateReceiver alloc] init];
        receiver.text = &body;
        receiver.limit = max_bytes;
        return run([NSString stringWithUTF8String:url.c_str()], receiver, 0, timeout_ms, false);
    }

    int download(const std::string& url, const std::filesystem::path& file, std::uint64_t max_bytes, const std::atomic_bool& cancel,
                 const std::function<void(std::uint64_t)>& progress) override {
        std::error_code error;
        std::uint64_t have = std::filesystem::exists(file, error) ? std::filesystem::file_size(file, error) : 0u;
        if (error || have > max_bytes) {
            std::filesystem::remove(file, error);
            have = 0;
        }
        if (have == max_bytes && have > 0u) return 416;
        std::FILE* out = std::fopen(file.c_str(), have > 0u ? "ab" : "wb");
        if (out == nullptr) return -1;
        NebulaUpdateReceiver* receiver = [[NebulaUpdateReceiver alloc] init];
        receiver.file = out;
        receiver.written = have;
        receiver.limit = max_bytes;
        receiver.cancel = &cancel;
        receiver.progress = &progress;
        int status = run([NSString stringWithUTF8String:url.c_str()], receiver, have, 15000, true);
        if (std::fflush(out) != 0) status = -1;
        std::fclose(out);
        return status;
    }

    bool unreachable(const std::string& url) override { return !proxied() && posix_unreachable(url, 1000); }
};

}  // namespace

Transport& platform_transport() {
    static AppleTransport transport;
    return transport;
}

}  // namespace galaxy::update::detail
