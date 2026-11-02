// SPDX-License-Identifier: GPL-3.0-only
#include "Game/System/SaveFileFormat.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace NebulaSaveFormat;
void check(bool value) { if (!value) throw std::runtime_error("save format invariant"); }
void store(std::vector<uint8_t>& data,size_t offset,uint32_t value,bool big) {
    for (unsigned i = 0; i < 4; ++i) data[offset+i] = uint8_t(value >> (8*(big ? 3-i : i)));
}
void seal(std::vector<uint8_t>& data,bool big) { store(data,0,checksum(data.data()+4,data.size()-4,big),big); }
std::vector<uint8_t> synthetic(uint32_t version,bool big) {
    std::vector<uint8_t> data(96);
    store(data,4,version,big);store(data,8,1,big);store(data,12,96,big);
    std::memcpy(data.data()+16,"mario1",7);store(data,28,32,big);
    data[32]=1;data[33]=1;
    store(data,36,0x504C4159,big);store(data,40,0x12345678,big);store(data,44,20,big);
    data[48]=7; // Synthetic progress, no owner-save bytes.
    seal(data,big);return data;
}
void tests() {
    for (const auto choice : {std::pair{2u,false},std::pair{2u,true},std::pair{3u,false}}) {
        auto data=synthetic(choice.first,choice.second);
        const auto unchanged=data;
        Header header;
        check(validate(data.data(),data.size(),header)==Error::None && header.version==choice.first);
        const auto expected=choice.second ? Format::Wii2 : choice.first==2 ? Format::Switch2 : Format::Native3;
        check(header.format==expected && header.entries==1 && header.size==96 && data==unchanged);
        for (size_t length=0;length<data.size();++length) {
            Header sentinel;sentinel.version=99;
            check(validate(data.data(),length,sentinel)!=Error::None && sentinel.version==99);
        }
        auto broken=data;store(broken,28,0xFFFFFFF0u,choice.second);seal(broken,choice.second);
        check(validate(broken.data(),broken.size(),header)==Error::Table);
        broken=data;store(broken,44,0,choice.second);seal(broken,choice.second);
        check(validate(broken.data(),broken.size(),header)==Error::Chunk);
        broken=data;broken[50]^=1;
        check(validate(broken.data(),broken.size(),header)==Error::Checksum);
    }
    Header header;
    check(validate(nullptr,111360,header)==Error::EncryptedExport);
    check(std::strstr(message(Error::EncryptedExport),"plain GameData.bin")!=nullptr);
    uint32_t random=0x942a3811u;
    auto original=synthetic(2,false);
    for (unsigned i=0;i<10000;++i) {
        auto data=original;
        random=random*1664525u+1013904223u;
        data[random%data.size()]^=uint8_t((random>>24)|1);
        seal(data,false);
        const auto copy=data;
        (void)validate(data.data(),data.size(),header);
        check(data==copy);
    }
    std::cout<<"Synthetic Wii2/Switch2/native3, every truncation, invalid tables/chunks/checksums and10000 bounded mutations pass\n";
}
}
int main(int argc,char** argv) {
    try {
        tests();
        if (argc==2) {
            std::ifstream in(argv[1],std::ios::binary);
            check(in.is_open());
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
            Header header;
            const auto error=validate(bytes.data(),bytes.size(),header);
            std::cout<<"private_save_validation="<<int(error)<<" version="<<header.version
                     <<" entries="<<header.entries<<" size="<<header.size<<'\n';
            check(error==Error::None);
        }
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
