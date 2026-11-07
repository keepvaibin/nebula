// Camera-space model blocks must stay fixed in the world under camera rotation.
#include "render/core/frame_core.h"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace galaxy::render;
using namespace galaxy::render::core;
Affine34 view(float yaw) { float c=std::cos(yaw),s=std::sin(yaw); return {c,0,s,0,0,1,0,0,-s,0,c,0}; }
int main() {
 auto a=view(0), b=view(.1f); Affine34 world={1,0,0,500,0,1,0,200,0,0,1,-10000};
 auto ja=affine_multiply(a,world), jb=affine_multiply(b,world);
 NebulaFrameCamera ca{},cb{}; std::memcpy(ca.view,a.data(),48);std::memcpy(cb.view,b.data(),48);ca.role=cb.role=NEBULA_CAMERA_SCENE;
 NebulaFrameBlock ba{},bb{};ba.joints=reinterpret_cast<const float(*)[4]>(ja.data());bb.joints=reinterpret_cast<const float(*)[4]>(jb.data());
 ba.joint_count=bb.joint_count=1;ba.slot=bb.slot=42;ba.generation=bb.generation=1;ba.camera=bb.camera=0;ba.calc_mode=bb.calc_mode=1;
 NebulaFrameList la{},lb{};la.cameras=&ca;lb.cameras=&cb;la.camera_count=lb.camera_count=1;la.blocks=&ba;lb.blocks=&bb;la.block_count=lb.block_count=1;
 ListData da{},db{};da.list=&la;db.list=&lb;da.serial=1;db.serial=2;db.block_camera={0};Pairing pairing;pairing.pair(db,&da);pairing.prepare_motion(db,da);
 auto camera=camera_at(db.camera_motion[0],.5f);auto expected=affine_multiply(camera,world);float out[3][4]{};
 joints_at(db,bb,0,.5f,&camera,out);
 float error=0;for(int i=0;i<12;++i)error=std::max(error,std::fabs((&out[0][0])[i]-expected[i]));
 std::printf("camera-relative static model midpoint max error: %.6f\n",error);
 if (error > .02f) return 1;
 NebulaFrameItem ia{},ib{};ia.slot=ib.slot=7;ia.generation=ib.generation=1;ia.flags=NEBULA_ITEM_RING;ib.flags=NEBULA_ITEM_RING|NEBULA_ITEM_DISCRETE_VERTICES;
 ia.light_set=ib.light_set=NEBULA_NONE32;
 NebulaFrameConstants constants{};NebulaFrameView frame_view{};
 la.items=&ia;lb.items=&ib;la.item_count=lb.item_count=1;
 la.constants=lb.constants=&constants;la.constants_count=lb.constants_count=1;
 la.views=lb.views=&frame_view;la.view_count=lb.view_count=1;
 ListData discrete{};discrete.list=&lb;Pairing discrete_pair;discrete_pair.pair(discrete,&da);
 if (discrete.item_before[0] != NEBULA_NONE32 || discrete.camera_before[0] != 0u) return 2;
 ib.flags=NEBULA_ITEM_RING;ListData continuous{};continuous.list=&lb;Pairing continuous_pair;continuous_pair.pair(continuous,&da);
 if (continuous.item_before[0] != 0u) return 3;

 NebulaFrameItem duplicates[2]={ia,ia};lb.items=duplicates;lb.item_count=2;
 ListData duplicated{};duplicated.list=&lb;Pairing duplicate_pair;duplicate_pair.pair(duplicated,&da);
 if (duplicated.item_before[0] != 0u || duplicated.item_before[1] != NEBULA_NONE32) return 4;
 lb.items=&ib;lb.item_count=1;ib.textures=123;
 ListData switched{};switched.list=&lb;Pairing switched_pair;switched_pair.pair(switched,&da);
 if (switched.item_before[0] != NEBULA_NONE32) return 5;
 NebulaFrameCamera old_cameras[2]={ca,ca};old_cameras[1].role=NEBULA_CAMERA_SHADOW;
 NebulaFrameCamera new_camera=old_cameras[1];la.cameras=old_cameras;la.camera_count=2;lb.cameras=&new_camera;lb.camera_count=1;
 la.block_count=lb.block_count=la.item_count=lb.item_count=0;
 ListData shadow{};shadow.list=&lb;Pairing shadow_pair;shadow_pair.pair(shadow,&da);
 if (shadow.camera_before[0] != 1u) return 6;
 la.block_count=lb.block_count=la.item_count=lb.item_count=1;
 ba.camera=bb.camera=NEBULA_NONE32;ib.textures=ia.textures;ia.light_set=ib.light_set=0;
 NebulaFrameLightSet old_light{},new_light{};old_light.camera_plus_one=1;new_light.camera_plus_one=1;
 la.light_sets=&old_light;lb.light_sets=&new_light;la.light_set_count=lb.light_set_count=1;
 ListData wrong_light{};wrong_light.list=&lb;Pairing light_pair;light_pair.pair(wrong_light,&da);
 if (wrong_light.light_set_before[0] != NEBULA_NONE32) return 7;
 old_light.camera_plus_one=2;
 ListData right_light{};right_light.list=&lb;Pairing right_pair;right_pair.pair(right_light,&da);
 if (right_light.light_set_before[0] != 0u) return 8;
 std::puts("PASS: explicit light camera survives camera reorder and rejects the wrong camera.");
 std::puts("PASS: shadow camera identity, discrete texture changes, and one-use draw pairing.");
 std::puts("PASS: regenerated shadow topology stays discrete; its camera still pairs.");
 return 0;
}
