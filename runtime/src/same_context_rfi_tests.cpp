#include "galaxy/same_context_rfi.h"
#include <iostream>
#include <stdexcept>

int main() {
    using galaxy::interrupt::CheckpointRfiProof;
    galaxy::PpcContext context{};
    constexpr std::uint32_t pc=0x80343B24u,thread=0x80650878u;
    context.pc=pc; context.msr=0xB032u;
    for(unsigned i=0;i<32;++i) {
        context.gpr[i]=i*0x103u;
        context.fpr_bits[i]=0x4000000000000000ULL+i;
        context.ps1_bits[i]=0x3FF0000000000000ULL+i;
    }
    for(unsigned i=0;i<8;++i) context.gqr[i]=i;
    for(unsigned i=0;i<16;++i) context.segment_registers[i]=i*7u;
    const auto baseline=context;
    unsigned checks=0;
    const auto require=[&](bool value) {
        ++checks; if(!value) throw std::runtime_error("same-context RFI admission fixture failed");
    };
    const auto match=[&](const CheckpointRfiProof& proof) {
        return proof.matches(context,pc,4u,thread,thread&0x3FFFFFFFu,3u,true);
    };
    CheckpointRfiProof proof(&context,thread,pc,3u);
    require(!match(proof)); proof.capture(context,4u,thread,pc); require(match(proof));
    for(unsigned i=0;i<32;++i) {
        context.gpr[i]^=1u; require(!match(proof)); context=baseline;
        context.fpr_bits[i]^=1u; require(!match(proof)); context=baseline;
        context.ps1_bits[i]^=1u; require(!match(proof)); context=baseline;
    }
    for(unsigned i=0;i<8;++i){context.gqr[i]^=1u;require(!match(proof));context=baseline;}
    for(unsigned i=0;i<16;++i){context.segment_registers[i]^=1u;require(!match(proof));context=baseline;}
    for(auto member : {&galaxy::PpcContext::cr,&galaxy::PpcContext::lr,&galaxy::PpcContext::ctr,
                      &galaxy::PpcContext::xer,&galaxy::PpcContext::fpscr,&galaxy::PpcContext::hid2,
                      &galaxy::PpcContext::msr,&galaxy::PpcContext::reserved_address,
                      &galaxy::PpcContext::reserved_value,&galaxy::PpcContext::pc}) {
        context.*member ^=1u;require(!match(proof));context=baseline;
    }
    require(!proof.matches(context,pc+4,4,thread,thread&0x3FFFFFFFu,3,true));
    require(!proof.matches(context,pc,8,thread,thread&0x3FFFFFFFu,3,true));
    require(!proof.matches(context,pc,4,thread+4,thread&0x3FFFFFFFu,3,true));
    require(!proof.matches(context,pc,4,thread,(thread&0x3FFFFFFFu)+4,3,true));
    require(!proof.matches(context,pc,4,thread,thread&0x3FFFFFFFu,4,true));
    require(!proof.matches(context,pc,4,thread,thread&0x3FFFFFFFu,3,false));
    galaxy::PpcContext different_native=context;
    require(!proof.matches(different_native,pc,4,thread,thread&0x3FFFFFFFu,3,true));
    // Timer/exception SPR changes stay live; the proof does not restore them.
    context.decrementer_start_ticks=9999; context.decrementer_start_value=1234;
    context.time_base_offset=4321; context.spr[26]=pc; context.spr[27]=context.msr;
    require(match(proof)); require(context.decrementer_start_ticks==9999);
    context=baseline; proof.capture(context,4u,thread,pc);require(!match(proof));
    CheckpointRfiProof retry(&context,thread,pc,3u);
    retry.capture(context,7u,thread,pc);require(!match(retry));
    retry.capture(context,4u,thread,pc);retry.capture(context,7u,thread,pc);require(match(retry));
    CheckpointRfiProof wrong(&context,thread,pc,3u);
    wrong.capture(context,4u,thread,pc+4);require(!match(wrong));
    using galaxy::interrupt::validated_checkpoint_rfi_source;
    require(validated_checkpoint_rfi_source(thread,0x804A381Cu,thread));
    require(!validated_checkpoint_rfi_source(0u,0u,thread));
    require(!validated_checkpoint_rfi_source(thread+4,0x804A381Cu,thread));
    require(!validated_checkpoint_rfi_source(thread,0x804A3818u,thread));
    require(!validated_checkpoint_rfi_source(0u,0x804A381Cu,0u));
    CheckpointRfiProof changed_thread(&context,thread,pc,3u,0x80651234u);
    changed_thread.capture(context,4u,thread,pc,0x80651238u);
    require(!changed_thread.matches(context,pc,4u,thread,thread&0x3FFFFFFFu,3u,true,0x80651234u));
    CheckpointRfiProof returned_thread(&context,thread,pc,3u,0x80651234u);
    returned_thread.capture(context,4u,thread,pc,0x80651234u);
    require(returned_thread.matches(context,pc,4u,thread,thread&0x3FFFFFFFu,3u,true,0x80651234u));
    require(!returned_thread.matches(context,pc,4u,thread,thread&0x3FFFFFFFu,3u,true,0x80651238u));
    for(const auto range : {std::pair{0x8000C53Cu,0x8000C5C0u},std::pair{0x802617B4u,0x80261854u},
                           std::pair{0x80443904u,0x80443980u},std::pair{0x80448710u,0x80448790u},
                           std::pair{0x80496A60u,0x80496A74u}}) {
        require(!galaxy::interrupt::checkpoint_native_helper_range(range.first-4u));
        require(galaxy::interrupt::checkpoint_native_helper_range(range.first));
        require(galaxy::interrupt::checkpoint_native_helper_range(range.second-4u));
        require(!galaxy::interrupt::checkpoint_native_helper_range(range.second));
    }
    std::cout << checks << " architectural identity/ownership/clock checks passed\n";
}
