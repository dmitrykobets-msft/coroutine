/**
 * @author github.com/luncliff (luncliff@gmail.com)
 */
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace std;

//
//  interface which doesn't require coroutine
//
using message_t = uint64_t;
using callback_t = void* (*)(void* context, message_t msg);

struct opaque_t;

opaque_t* start_messaging(void* context, callback_t on_message);
void stop_messaging(opaque_t*);
void send_message(opaque_t*, message_t msg);

//
//  user code to use the interface
//

void* update_location(void* ptr, message_t msg) {
    auto* pmsg = static_cast<message_t*>(ptr);
    *pmsg = msg; // update the reference and return it
    return pmsg;
}

int main(int, char*[]) {
    message_t target = 0;
    auto session = start_messaging(&target, //
                                   &update_location);
    assert(session != nullptr);

    for (auto m : {1u, 2u, 3u}) {
        send_message(session, m); // the callback (update)
        assert(target == m);      // must have changed the memory location
    }
    stop_messaging(session);
    return EXIT_SUCCESS;
}

//
//  The implementation uses coroutine
//
#include "internal/channel.hpp"
#include <coroutine/return.h> // includes `coroutine_traits<void, ...>`

using channel_t = coro::channel<message_t>;

opaque_t* start_messaging(void* ctx, callback_t on_message) {
    auto* ch = new (std::nothrow) channel_t{};
    if (ch == nullptr)
        return nullptr;

    // attach a receiver coroutine to the channel
    [ch](void* context, callback_t callback) mutable -> void {
        puts("start receiving ...");
        while (true) {
            auto [msg, ok] = co_await ch->read();
            if (ok == false) {
                puts("stopped ...");
                co_return;
            }
            // received. invoke the callback
            puts("received");
            context = callback(context, msg);
        }
    }(ctx, on_message);
    return reinterpret_cast<opaque_t*>(ch);
}

void stop_messaging(opaque_t* ptr) {
    auto ch = reinterpret_cast<channel_t*>(ptr);
    delete ch;
}

void send_message(opaque_t* ptr, message_t m) {
    // spawn a sender coroutine
    [](channel_t* ch, message_t msg) mutable -> void {
        // msg will be 'moved' to reader coroutine. so it must be `mutable`
        if (co_await ch->write(msg) == false)
            ; // the channel is going to destruct ...
        puts("sent");
    }(reinterpret_cast<channel_t*>(ptr), std::move(m));
}
