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
 // Sets of different counts pair when the lights they share follow the camera alike.
 std::uint32_t follow_old = 3u, follow_new = 3u;
 old_light.count = 2u;new_light.count = 3u;la.light_follow = &follow_old;lb.light_follow = &follow_new;
 ListData grown_light{};grown_light.list=&lb;Pairing grown_pair;grown_pair.pair(grown_light,&da);
 if (grown_light.light_set_before[0] != 0u) return 9;
 follow_new = 1u;
 ListData moved_light{};moved_light.list=&lb;Pairing moved_pair;moved_pair.pair(moved_light,&da);
 if (moved_light.light_set_before[0] != NEBULA_NONE32) return 10;
 // A ring item pairs with the item of its own part number, never with the one that drew in its place.
 NebulaFrameItem parted[2]={ia,ia};parted[1].part=2;ib.part=2;
 la.items=parted;la.item_count=2;lb.items=&ib;lb.item_count=1;
 ListData same_part{};same_part.list=&lb;Pairing part_pair;part_pair.pair(same_part,&da);
 if (same_part.item_before[0] != 1u) return 11;
 ib.part=1;
 ListData other_part{};other_part.list=&lb;Pairing other_pair;other_pair.pair(other_part,&da);
 if (other_part.item_before[0] != NEBULA_NONE32) return 12;
 // Its vertices blend only when vertex count, index count and stride are the same.
 NebulaFrameItem ring_old{},ring_new{};
 ring_old.flags=ring_new.flags=NEBULA_ITEM_RING;ring_old.mesh=ring_new.mesh=24;ring_old.ring_count=ring_new.ring_count=8;ring_old.count=ring_new.count=18;
 if (!ring_shape_matches(ring_new,ring_old)) return 13;
 ring_new.ring_count=10;
 if (ring_shape_matches(ring_new,ring_old)) return 14;
 ring_new.ring_count=8;ring_new.count=12;
 if (ring_shape_matches(ring_new,ring_old)) return 15;
 ring_new.count=18;ring_new.mesh=32;
 if (ring_shape_matches(ring_new,ring_old)) return 16;
 ring_new.mesh=24;ring_old.flags=0u;
 if (ring_shape_matches(ring_new,ring_old)) return 17;
 // OTHER-role cameras listed in another order pair by their view matrices; the game camera pairs by its role.
 {
  NebulaFrameCamera other_old[3]={ca,ca,ca},other_new[3]={ca,ca,ca};
  other_old[0].role=NEBULA_CAMERA_SCENE;other_old[1].role=other_old[2].role=NEBULA_CAMERA_OTHER;
  other_new[0].role=NEBULA_CAMERA_SCENE;other_new[1].role=other_new[2].role=NEBULA_CAMERA_OTHER;
  other_old[1].view[0][3]=100.0f;other_old[2].view[0][3]=5000.0f;
  other_new[1].view[0][3]=5000.5f;other_new[2].view[0][3]=100.5f;
  la.cameras=other_old;la.camera_count=3;lb.cameras=other_new;lb.camera_count=3;
  la.block_count=lb.block_count=la.item_count=lb.item_count=0;
  ListData swapped_cameras{};swapped_cameras.list=&lb;Pairing swapped_pair;swapped_pair.pair(swapped_cameras,&da);
  if (swapped_cameras.camera_before[0] != 0u || swapped_cameras.camera_before[1] != 2u || swapped_cameras.camera_before[2] != 1u) return 18;
  // One view fewer: the one left pairs with the view it is nearest, the other old view stays unused.
  lb.camera_count=2;other_new[1]=other_new[2];
  ListData fewer_cameras{};fewer_cameras.list=&lb;Pairing fewer_pair;fewer_pair.pair(fewer_cameras,&da);
  if (fewer_cameras.camera_before[0] != 0u || fewer_cameras.camera_before[1] != 1u) return 19;
  // Identical views pair in list order.
  other_old[1].view[0][3]=other_old[2].view[0][3]=100.0f;other_new[1].view[0][3]=other_new[2].view[0][3]=100.0f;lb.camera_count=3;
  ListData same_cameras{};same_cameras.list=&lb;Pairing same_pair;same_pair.pair(same_cameras,&da);
  if (same_cameras.camera_before[1] != 1u || same_cameras.camera_before[2] != 2u) return 20;
 }
 std::puts("PASS: other-role cameras pair by the nearest view matrix, the game camera by its role.");
 std::puts("PASS: ring items pair by part number and blend vertices only at an unchanged shape.");
 std::puts("PASS: explicit light camera survives camera reorder and rejects the wrong camera.");
 std::puts("PASS: shadow camera identity, discrete texture changes, and one-use draw pairing.");
 std::puts("PASS: regenerated shadow topology stays discrete; its camera still pairs.");
 return 0;
}
