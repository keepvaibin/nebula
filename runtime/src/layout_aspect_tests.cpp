#include "galaxy/layout_aspect_cinema.h"
#include "galaxy/layout_aspect_fits.h"
#include "galaxy/layout_aspect_home.h"
#include <iostream>
#include <map>
#include <set>
#include <vector>

struct PaneFixture {
    std::map<std::uint32_t, std::string> names;
    std::map<std::uint32_t, std::vector<std::uint32_t>> tree;
    mutable std::map<std::uint32_t, float> values;
    std::set<std::uint32_t> hidden;
    bool visible(std::uint32_t pane) const { return !hidden.contains(pane); }
    std::map<std::uint32_t, std::uint8_t> bytes;
    std::uint8_t u8(std::uint32_t address) const { return bytes.at(address); }
    bool named(std::uint32_t pane, std::string_view name) const {
        return names.at(pane) == name;
    }
    template <typename Visitor> void children(std::uint32_t pane, Visitor visitor) const {
        const auto found = tree.find(pane);
        if (found != tree.end()) for (auto child : found->second) visitor(child);
    }
    void f32(std::uint32_t address, float value) const { values[address] = value; }
    float f32(std::uint32_t address) const { return values.at(address); }
};
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        check(galaxy::safety_surround_intensity(255u,0.0f)==1.0f &&
              galaxy::safety_surround_intensity(255u,1.0f)==0.0f &&
              galaxy::safety_surround_intensity(0u,0.0f)==0.0f,
              "safety surround must respect backing alpha and fade endpoints");
        check(std::abs(galaxy::safety_surround_intensity(255u,0.5f)-128.0f/255.0f)<1e-6f,
              "surround must match the guest's byte-quantized black fade");
        PaneFixture fixture;
        fixture.names = {{0x100,"RootPane"},{0x200,"CinemaFrame"},{0x300,"FrameU"},
                         {0x400,"FrameD"},{0x500,"Unrelated"},{0x600,"Base"}};
        fixture.tree = {{0x100,{0x200,0x500}},{0x200,{0x300,0x400}},{0x500,{0x600}}};
        fixture.values[0x350] = 60.0f;
        fixture.values[0x450] = 60.0f;
        fixture.values[0x330] = 220.0f; // animated vertical translation
        for (auto aspect : {21.0/9.0, 32.0/9.0, 16.0/10.0, 16.0/9.0}) {
            const auto policy = galaxy::make_experimental_ultrawide_aspect(aspect);
            const auto extent = galaxy::layout_aspect_extent(*policy);
            for (int frame = 0; frame != 100; ++frame)
                galaxy::extend_rmge01_layout_backings(fixture, 0x100, extent);
            check(fixture.values.at(0x34C) == extent.width && fixture.values.at(0x44C) == extent.width,
                  "animated bars must follow aspect without accumulated scaling");
            check(fixture.values.at(0x350)==60.0f && fixture.values.at(0x450)==60.0f && fixture.values.at(0x330)==220.0f,
                  "bar height and animation translation must remain authored");
            check(!fixture.values.contains(0x64C), "unrelated Base must remain untouched");
        }
        const auto taller = galaxy::layout_aspect_extent(*galaxy::make_experimental_ultrawide_aspect(16.0/10.0));
        for (float height : {60.0f, 155.0f, 250.0f, 155.0f, 60.0f}) {
            // Animation/CalculateMtx restore these inputs before every hook.
            fixture.values[0x350u] = fixture.values[0x450u] = height;
            fixture.values[0x3A0u] = 245.0f; fixture.values[0x4A0u] = -245.0f;
            fixture.values[0x390u] = fixture.values[0x490u] = 0.0f;
            galaxy::fit_rmge01_cinema_vertical(fixture,0x100,taller);
            check(fixture.values.at(0x3A0u) == 245.0f + taller.vertical_margin_delta &&
                  fixture.values.at(0x4A0u) == -245.0f - taller.vertical_margin_delta,
                  "taller cinema bars must anchor to the viewport edges without changing animation translation");
            if (height == 60.0f) check(fixture.values.at(0x350u) == height,
                  "ordinary cinema frame retains its authored height");
            if (height == 250.0f) check(std::abs(fixture.values.at(0x3A0u)-fixture.values.at(0x350u)+5.0f)<1e-5f,
                  "fully closed bars must still overlap at the center, without a gap");
        }
        fixture.names[0x200] = "PrologueDemo";
        fixture.names[0x300] = "Base";
        fixture.names[0x400] = "Picture01";
        fixture.values.clear();
        const auto extent = galaxy::layout_aspect_extent(*galaxy::make_experimental_ultrawide_aspect(21.0/9.0));
        galaxy::extend_rmge01_layout_backings(fixture,0x100,extent);
        check(fixture.values.at(0x34C)==extent.width && !fixture.values.contains(0x44C),
              "storybook must extend only paper backing, preserving illustrations");
        fixture.names[0x400] = "Fade";
        galaxy::extend_rmge01_layout_backings(fixture,0x100,extent);
        check(fixture.values.at(0x44C)==extent.width && fixture.values.at(0x450)==extent.height,
              "storybook transition mask must cover the extended backing");
        fixture.names[0x200] = "WiiRemoteStrap";
        fixture.names[0x500] = "PicBG";
        fixture.values.clear();
        galaxy::extend_rmge01_layout_backings(fixture,0x100,extent);
        check(fixture.values.at(0x54C)==extent.width && fixture.values.at(0x550)==extent.height && fixture.values.size()==2,
              "safety surround must preserve centered illustrations");
        fixture.names[0x200] = "SMGTitleLogo";
        fixture.names[0x500] = "PicFlash";
        fixture.values.clear();
        galaxy::extend_rmge01_layout_backings(fixture,0x100,extent);
        check(fixture.values.size()==2 && fixture.values.at(0x54C)==extent.width,
              "title flash must extend only the identified fullscreen pane");
        fixture.names={{0x100,"RootPane"},{0x200,"back_00"},{0x300,"bar_00"},
                       {0x400,"bar_10"},{0x500,"bar_line_00"},{0x600,"Close"},
                       {0x700,"B_btn_00"}};
        fixture.tree={{0x100,{0x200,0x300,0x400}},{0x300,{0x500,0x600,0x700}}};
        fixture.values.clear();
        fixture.values[0x350u]=80.0f;fixture.values[0x450u]=100.0f;
        const auto wide=galaxy::layout_aspect_extent(*galaxy::make_experimental_ultrawide_aspect(32.0/9.0));
        for(unsigned frame=0;frame<100;++frame)
            check(galaxy::extend_rmge01_home_backings(fixture,0x100,wide),"main Home identity must qualify");
        check(fixture.values.at(0x24Cu)==wide.width && fixture.values.at(0x34Cu)==wide.width &&
              fixture.values.at(0x54Cu)==wide.width && fixture.values.at(0x74Cu)==wide.width &&
              fixture.values.at(0x350u)==80.0f && fixture.values.at(0x450u)==100.0f &&
              !fixture.values.contains(0x64Cu),
              "Home backings and bar hit regions must cover the scene, leaving buttons and height intact");
        for (int frame=0;frame<100;++frame) {
            fixture.values[0x3A0u]=228.0f;fixture.values[0x4A0u]=-228.0f;
            fixture.values[0x330u]=228.0f;fixture.values[0x430u]=-228.0f;
            galaxy::offset_rmge01_home_bar_vertical(fixture,0x300u,taller.vertical_margin_delta);
            galaxy::offset_rmge01_home_bar_vertical(fixture,0x400u,taller.vertical_margin_delta);
            check(fixture.values.at(0x3A0u)==228.0f+taller.vertical_margin_delta &&
                  fixture.values.at(0x4A0u)==-228.0f-taller.vertical_margin_delta &&
                  fixture.values.at(0x330u)==228.0f && fixture.values.at(0x430u)==-228.0f,
                  "Home global bar anchors must retain local animation and avoid accumulation");
            check(!galaxy::offset_rmge01_home_bar_vertical(fixture,0x600u,taller.vertical_margin_delta),
                  "unrelated Home buttons are not shifted directly");
        }
        fixture.names[0x200]="Pointer";
        check(!galaxy::extend_rmge01_home_backings(fixture,0x100,wide),"Home pointer layouts must not qualify");
        fixture.names={{0x100,"RootPane"},{0x200,"PlayerLeft"},{0x300,"LeftCounter"},{0x500,"CenterPlayerLeft"}};
        fixture.tree={{0x100,{0x200,0x500}},{0x200,{0x300}}};
        for (auto pane : {0x200u,0x300u,0x500u}) {
            fixture.values[pane+0x90u]=12.0f;fixture.values[pane+0xA0u]=17.0f;
        }
        const auto player_left = galaxy::rmge01_layout_fits("PlayerLeft");
        check(player_left.size() == 1u && player_left[0].pane == "PlayerLeft" &&
              player_left[0].fit == (galaxy::kFitLeft | galaxy::kFitBottom),
              "PlayerLeft must anchor its counter branch to the lower left");
        check(galaxy::rmge01_layout_fits("Player").empty() && galaxy::rmge01_layout_fits("PlayerLeftX").empty() &&
              galaxy::rmge01_layout_fits("CometScreenFilter").size() == 5u,
              "layout lookup must match whole names only");
        check(galaxy::apply_rmge01_layout_fit(fixture,0x200,player_left[0],extent),
              "an anchored branch must report its move");
        check(fixture.values.at(0x290)==12.0f-extent.horizontal_margin_delta &&
              fixture.values.at(0x390)==fixture.values.at(0x290) && fixture.values.at(0x590)==12.0f,
              "edge HUD branch and descendants must move together; centered variants must remain centered");
        fixture.tree[0x200] = {0x300u, 0x600u};
        fixture.names[0x600] = "Hidden";
        fixture.hidden = {0x600u};
        fixture.values[0x690u] = 3.0f;
        galaxy::apply_rmge01_layout_fit(fixture,0x200,player_left[0],extent);
        check(fixture.values.at(0x690u)==3.0f, "an invisible branch keeps its stale matrix untouched");
        fixture.hidden.clear();
        {
            PaneFixture fill;
            for (unsigned i = 0; i < 12u; ++i) fill.values[0x184u+i*4u]=static_cast<float>(i+1u);
            const galaxy::LayoutFitRule rule{"GameOver","Color",galaxy::kFitWidth|galaxy::kFitHeight};
            const galaxy::LayoutAspectExtent ultrawide{1216.0f,456.0f,304.0f,0.0f};
            check(!galaxy::apply_rmge01_layout_fit(fill,0x100,rule,ultrawide),"a fill never moves references");
            check(fill.values.at(0x184u)==2.0f && fill.values.at(0x190u)==8.0f &&
                  fill.values.at(0x194u)==5.0f && fill.values.at(0x1A4u)==9.0f,
                  "a fill scales its x row to the canvas and leaves y and z for a wide canvas");
            const galaxy::LayoutFitRule art{"MapGalaxyBg","PicStar",galaxy::kFitCover};
            const galaxy::LayoutAspectExtent narrow{608.0f,608.0f,0.0f,76.0f};
            galaxy::apply_rmge01_layout_fit(fill,0x100,art,narrow);
            check(std::abs(fill.values.at(0x184u)-2.0f*(608.0f/456.0f))<1e-5f &&
                  std::abs(fill.values.at(0x194u)-5.0f*(608.0f/456.0f))<1e-5f,
                  "artwork covers a taller canvas with one uniform scale");
            PaneFixture frame;
            frame.values[0x184u]=1.2f; frame.values[0x198u]=1.2f;
            const galaxy::LayoutFitRule border{"PointerGuidance","WinPointFrame1",galaxy::kFitFrame,720.0f,378.0f};
            galaxy::apply_rmge01_layout_fit(frame,0x100,border,ultrawide);
            check(std::abs(frame.values.at(0x14Cu)-(720.0f+608.0f/1.2f))<1e-3f && frame.values.at(0x150u)==378.0f,
                  "a screen frame grows by the extra canvas in its own units");
            galaxy::apply_rmge01_layout_fit(frame,0x100,border,galaxy::LayoutAspectExtent{});
            check(frame.values.at(0x14Cu)==720.0f, "a screen frame returns to its authored size at 16:9");
            PaneFixture bar;
            bar.bytes[0x1B6u] = 4u;
            bar.values[0x150u] = 160.0f;
            bar.values[0x194u] = 0.0f; bar.values[0x198u] = 1.0f; bar.values[0x19Cu] = 0.0f; bar.values[0x1A0u] = 157.0f;
            const galaxy::LayoutFitRule band{"PauseMenu","PicStagebase",galaxy::kFitExtendUp};
            galaxy::apply_rmge01_layout_fit(bar,0x100,band,narrow);
            const float m11 = bar.values.at(0x198u), m13 = bar.values.at(0x1A0u);
            check(std::abs((m13 - m11 * 80.0f) - 77.0f) < 1e-3f && std::abs((m13 + m11 * 80.0f) - (237.0f + 76.0f)) < 1e-3f,
                  "a top bar keeps its inner edge and reaches the taller canvas top");
        }
        for (unsigned i = 0; i < 12u; ++i) fixture.values[0x384u+i*4u]=static_cast<float>(i);
        fixture.values[0x390u]=12.0f-extent.horizontal_margin_delta;
        galaxy::copy_rmge01_pane_reference(fixture,0x300u,0x800u,1092.0f/608.0f);
        check(fixture.values.at(0x80Cu)==fixture.values.at(0x390u)*(1092.0f/608.0f) &&
              fixture.values.at(0x820u)==fixture.values.at(0x3A4u),
              "matrix references must use adjusted coordinates and retain the original wide conversion");
        std::cout << "layout aspect geometry checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
