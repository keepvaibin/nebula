/* 2710-net-http-size (registry: a struct of a later minor is read up to the fields this build knows). nh-size, granted
   net.local, sends two http requests at start to a closed port of this machine: "later" a request whose size is
   sizeof(LumaNetHttpRequest) + 16 (as a mod built against a later minor gives; LUMA_PENDING, an ERROR event follows)
   and "smaller" one of sizeof - 8 (LUMA_E_ARGUMENT, reported). */
#include "../loader/lt.h"
#include <luma_net.h>

static const LumaNetLocalService* net;

static void on_http(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static struct {
    LumaNetHttpRequest request;
    uint8_t later[16];
} big;

static int32_t send_one(uint32_t size) {
    LumaHandle h = 0;
    big.request.size = size;
    big.request.url = "http://127.0.0.1:9/";
    return net->http(lt_mod, &big.request, on_http, 0, &h);
}

static const LumaImport imports[] = {
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.3"),
};

LT_START({
    lt_fact("later", send_one((uint32_t)sizeof(LumaNetHttpRequest) + 16u));
    lt_fact("smaller", send_one((uint32_t)sizeof(LumaNetHttpRequest) - 8u));
})
LUMA_MOD(imports, start, 0);
