"""Generate the shared fixed catalog; --check is read-only and fails on drift.

Outputs: embedded_sw/.../apbTest/src/enemy_config.h, enemy_config.vh, enemy_catalog.vh.
Every numeric contract shared by C and RTL (capacities, fixed DDR ranges, sprite
sizes, effect animations, hitboxes, HP, waves) comes from config/enemy_scene.json.
"""
from pathlib import Path
import json,sys
R=Path(__file__).resolve().parents[1]
c=json.loads((R/'config/enemy_scene.json').read_text(encoding='utf-8-sig'));cap=c['enemy_capacity'];assets=c['assets'];mats=c['materials']
bcap=c['bullet_capacity'];bullets=c['bullet_materials']
draw=c['bullet_draw'];assert draw['width']==8 and draw['height']==12 and draw['member_dx']==20 and draw['source_xy']==[[0,0],[8,0],[0,16],[8,16]]
assert len(draw['hitboxes'])==4 and len(draw['material_to_style'])==4 and all(t in range(4) for t in draw['material_to_style'])
for x0,y0,x1,y1 in draw['hitboxes']:assert 0<=x0<x1<=8 and 0<=y0<y1<=12
triple=draw['triple'];assert triple=={'material':1,'style':1,'muzzles':[[23,56],[32,61],[40,56]],'anchor':[19,57],'offsets':[[0,0],[9,5],[17,0]],'span':[25,17]}
player=c['player']
pcap=c['player_bullet_capacity'];ptype=c['player_bullet_material_type']
fxcap=c['effect_capacity'];fxres=c['effect_death_reserve'];effects=c['effects']
hp=c['enemy_hp'];respawn=c['respawn_ms'];score=c['score']
phit=c['player_hitbox'];lives=c['player_lives'];invuln=c['invulnerable_ms'];fire=c['fire']
assert all(isinstance(hp[k],int) and 1<=hp[k]<=65535 and isinstance(respawn[k],int) and 1<=respawn[k]<=60000 for k in ('small','large'))
assert len(mats)==4
for i,m in enumerate(mats):
 x0,y0,x1,y1=m['hitbox']
 assert all(isinstance(v,int) for v in m['hitbox']) and 0<=x0<x1<=m['width'] and 0<=y0<y1<=m['height']
 kind='large' if i==3 else 'small'
 assert m['hp']==hp[kind] and m['respawn_ms']==respawn[kind] and m['score']==score[kind], 'material %d must match %s class'%(i,kind)
 assert m['width']==m['height']==(128 if i==3 else 64)
x0,y0,x1,y1=phit;assert 0<=x0<x1<=player['width'] and 0<=y0<y1<=player['height']
assert 1<=lives<=10 and 0<invuln<=10000  # GHD: 10 HP (HUD2 shows two digits; the old HUD clamps to 9)
assert pcap==16 and ptype==1, 'PB1.0 requires independent 16 slots and fixed original blue material'
assert bcap in (32,64,128,192,256,512,600,640,680,700,720,760,800,850) and len(bullets)==2, 'BU6: 10-bit rank field (<=1023)'
# GHD4: bank stride = 1024 x ceil(cells/1024): parity-striped RAM10 (1024 x 10) blocks; <= 3072 keeps the BRAM budget (6 blocks)
bank_words=1024*((3*(cap+bcap+16+17+8)+1023)//1024)
assert bank_words<=3072 and bank_words%2==0, 'scene table bank stride above 3072 words (BRAM budget)'
assert 64+bcap+16+8+17<=1024 and cap+bcap+16+17+8<=1023, 'compositor row list (1024) / rank field (10 bit) overflow'
assert fxcap==17 and c['effect_pool_capacity']==16 and 0<=fxres<=16, 'FX2: 16 transient slots plus persistent shield descriptor'
assert len(effects)==4 and [e['size'] for e in effects]==[64,128,64,128]
assert all(1<=e['frames']<=8 and 1<=e['frame_ms']<=1000 for e in effects)
assert 1<=cap<=65535 and [a['id'] for a in assets]==list(range(1,len(assets)+1)) and len(assets)<64
bases=[0]+[int(a['base'],16) for a in assets];lengths=[0]+[a['length'] for a in assets]
assert all(b%128==0 and n%128==0 and b+n<=0x10000000 for b,n in zip(bases,lengths))
assert all(bases[i]+lengths[i]<=bases[i+1] for i in range(1,len(assets)))
assert all(0<a.get('logical_length',a['length'])<=a['length'] for a in assets)
assert all(m['width']==16 and m['height']==32 and lengths[m['color_id']]==1024 and lengths[m['mask_id']]==128 for m in bullets)
assert player['width']==96 and player['height']==96 and lengths[player['color_id']]==18432 and lengths[player['mask_id']]==1152
assert 0<=player['x']<=1824 and 0<=player['y']<=984
for e in effects:
 s=e['size'];assert lengths[e['color_id']]==s*s*2*e['frames'] and lengths[e['alpha_id']]==s*s//2*e['frames'],e['name']
# Waves: fully on-screen, x multiple of 4 (row_list packs x/4 for effects spawned at the enemy), sizes by material.
waves=dict(c['waves'])
if waves.get('WAVE35')=='generated':
 gen=[]
 for k in range(5):gen.append({'material':3,'x':64+384*k,'y':(96+180*k)%900,'speed':[60,50,40,50,60][k]})
 for j in range(30):gen.append({'material':j%3,'x':4*round((16+j*62.5)/4),'y':(40+j*37)%980,'speed':[120,90,150][j%3]})
 waves['WAVE35']=gen
wave_names=['BOSS4','WAVE8','WAVE16','WAVE35','WAVE20','WAVE24','WAVE32','WAVE40','WAVE0']
assert list(waves)==wave_names
for name in wave_names:
 w=waves[name]
 assert len(w)<=cap and len(w)==int(name[4:]) if name!='BOSS4' else len(w)==4
 for e in w:
  size=mats[e['material']]['width']
  assert e['material'] in range(4) and e['x']%4==0 and 0<=e['x']<=1920-size and 0<=e['y']<=1080-size and 1<=e['speed']<=400,(name,e)
  # S1 RICH: optional role per row; RICH waves give every row a role compatible with its material.
  assert 'role' not in e or e['material'] in [c['rich']['role_material'][e['role']]]+c['rich'].get('role_material_alt',{}).get(e['role'],[]),(name,e)
 assert all('role' in e for e in w) or not any('role' in e for e in w),name
h=['/* Generated by tools/generate_enemy_config.py; edit config/enemy_scene.json. */','#ifndef ENEMY_CONFIG_H','#define ENEMY_CONFIG_H',f'#define ENEMY_CAPACITY {cap}u',f'#define ASSET_ID_MAX {len(assets)}u',f'#define ASSET_SUPPORTED_MASK 0x{sum(1<<a["id"] for a in assets if a["id"]<32):08X}u',f'#define ASSET_SUPPORTED_MASK_HI 0x{sum(1<<(a["id"]-32) for a in assets if a["id"]>=32):08X}u','#define ASSET_BASE_VALUES {'+','.join(f'0x{v:08X}u' for v in bases)+'}','#define ASSET_LENGTH_VALUES {'+','.join(str(v)+'u' for v in lengths)+'}',f'#define ENEMY_MATERIAL_COUNT {len(mats)}u','#define ENEMY_MATERIAL_VALUES {'+','.join('{'+','.join(str(m[k])+'u' for k in ['color_id','mask_id','width','height'])+'}' for m in mats)+'}','#endif','']
h[-2:-2]=[f'#define BULLET_CAPACITY {bcap}u', f'#define BULLET_MATERIAL_COUNT {len(bullets)}u',
 '#define BULLET_REQUIRED_ASSETS 0x00003C00u',
 '#define BULLET_MATERIAL_VALUES {'+','.join('{'+','.join(str(m[k])+'u' for k in ['color_id','mask_id','width','height'])+'}' for m in bullets)+'}']
h[-2:-2]=[f'#define PLAYER_{k.upper()} {v}u' for k,v in player.items()]+[f'#define PLAYER_REQUIRED_ASSETS 0x{(1<<player["color_id"])|(1<<player["mask_id"]):08X}u']
h[-2:-2]=[f'#define BULLET_DRAW_WIDTH {draw["width"]}u',f'#define BULLET_DRAW_HEIGHT {draw["height"]}u',f'#define BULLET_MEMBER_DX {draw["member_dx"]}u', '#define BULLET_HITBOX_VALUES {'+','.join(str(v)+'u' for v in draw['hitbox'])+'}', f'#define BULLET_PACKED_COLOR_CRC 0x{draw["packed_color_crc32"]:08x}u', f'#define BULLET_PACKED_MASK_CRC 0x{draw["packed_mask_crc32"]:08x}u']
h[-2:-2]=['#define BULLET_STYLE_COUNT 4u','#define BULLET_STYLE_BY_MATERIAL {'+','.join(str(v)+'u' for v in draw['material_to_style'])+'}','#define BULLET_STYLE_HITBOX_VALUES {'+','.join('{'+','.join(str(v)+'u' for v in h)+'}' for h in draw['hitboxes'])+'}']
h[-2:-2]=[f'#define PLAYER_BULLET_CAPACITY {pcap}u', f'#define PLAYER_BULLET_MATERIAL_TYPE {ptype}u']
h[-2:-2]=['#define ENEMY_HITBOX_VALUES {'+','.join('{'+','.join(str(v)+'u' for v in m['hitbox'])+'}' for m in mats)+'}',
 '#define ENEMY_HP_VALUES {'+','.join(str(m['hp'])+'u' for m in mats)+'}',
 '#define ENEMY_RESPAWN_MS_VALUES {'+','.join(str(m['respawn_ms'])+'u' for m in mats)+'}',
 '#define ENEMY_SCORE_VALUES {'+','.join(str(m['score'])+'u' for m in mats)+'}',
 '#define PLAYER_HITBOX_VALUES {'+','.join(str(v)+'u' for v in phit)+'}',
 f'#define PLAYER_LIVES {lives}u',f'#define PLAYER_INVULNERABLE_MS {invuln}u',
 f'#define EFFECT_CAPACITY {fxcap}u',f'#define EFFECT_POOL_CAPACITY {c["effect_pool_capacity"]}u',
 '#define EFFECT_FRAMES_VALUES {'+','.join(str(e['frames'])+'u' for e in effects)+'}',
 *[f'#define FX_{k.upper()} {v}u' for k,v in c['effect_limits'].items()],f'#define EFFECT_DEATH_RESERVE {fxres}u',f'#define EFFECT_TYPE_COUNT {len(effects)}u',
 '#define EFFECT_VALUES {'+','.join('{'+','.join(str(e[k])+'u' for k in ['color_id','alpha_id','size','frames','frame_ms'])+'}' for e in effects)+'}',
 f'#define EFFECT_REQUIRED_ASSETS 0x{sum((1<<e["color_id"])|(1<<e["alpha_id"]) for e in effects):08X}u',
 f'#define FIRE_GLOBAL_PERIOD_MS {fire["global_period_ms"]}u',f'#define FIRE_LARGE_PERIOD_MS {fire["large_period_ms"]}u',
 f'#define FIRE_MAX_SPAWN_PER_FRAME {fire["max_spawn_per_frame"]}u',f'#define FIRE_SMALL_SPEED {fire["small_speed"]}u',f'#define FIRE_LARGE_SPEED {fire["large_speed"]}u',
 f'#define FIRE_FAN_HALF_ANGLE_Q16 {fire["fan_half_angle_q16"]}u',
 f'#define WAVE_COUNT {len(wave_names)}u','#define WAVE_SIZES {'+','.join(str(len(waves[n]))+'u' for n in wave_names)+'}',
 '#define WAVE_NAMES {'+','.join('"'+n+'"' for n in wave_names)+'}',
 # S0: rows of all waves stored once (no padding to ENEMY_CAPACITY); WAVE_TABLES points at each wave's first row.
 f'#define WAVE_ROWS {sum(len(waves[n]) for n in wave_names)}u',
 '#define WAVE_VALUES {'+','.join('{'+','.join(str(e[k])+'u' for k in ['x','y','material','speed'])+'}' for n in wave_names for e in waves[n])+'}',
 '#define WAVE_ROLE_VALUES {'+','.join(str(c['rich']['roles'].index(e['role'])+1 if 'role' in e else 0)+'u' for n in wave_names for e in waves[n])+'}',
 f"#define WAVE_RICH_MASK 0x{sum(1<<i for i,n in enumerate(wave_names) if waves[n] and 'role' in waves[n][0]):02X}u",
 '#define WAVE_TABLES {'+','.join(f'wave_rows+{sum(len(waves[m]) for m in wave_names[:i])}' for i in range(len(wave_names)))+'}']
# S1 RICH fire parameters (roles 1..6; 0 = by material). Ring velocities: round(speed*cos/sin(2*pi*(k+rot/2)/n)).
rc=c['rich'];assert rc['roles']==['FANX','TRIPLE','THREE','BLUE3','RING2','CURTAIN'] and rc['ring_directions']==12
import math
ring=[(round(rc['ring_speed']*math.cos(2*math.pi*(k+0.5*r)/12)),round(rc['ring_speed']*math.sin(2*math.pi*(k+0.5*r)/12))) for r in (0,1) for k in range(12)]
h[-2:-2]=[*[f'#define RICH_ROLE_{n} {i+1}u' for i,n in enumerate(rc['roles'])],f'#define RICH_ROLE_COUNT {len(rc["roles"])+1}u',
 '#define RICH_ROLE_BY_MATERIAL {'+','.join(str(rc['roles'].index(n)+1)+'u' for n in rc['role_by_material'])+'}',
 '#define RICH_PERIOD_MS_VALUES {0u,'+','.join(str(rc['period_ms'][n])+'u' for n in rc['roles'])+'}',
 f"#define RICH_LITE_FANX_PERIOD_MS {rc['lite_fanx_period_ms']}u",f"#define RICH_CEASE_Y {rc['cease_y']}u",f"#define RICH_RING_MAX_CENTER_Y {rc['ring_max_center_y']}u",
 f"#define RICH_FRAME_BUDGET {rc['frame_budget_groups']}u",f"#define RICH_AGE_FRAMES {rc['age_frames']}u",f"#define RICH_LANE_GUARD_GAP {rc['lane_guard_gap']}u",
 f"#define RICH_FIRST_SHOT_MS {rc['first_shot_ms']}u"]
# One row per group of an event {dx,dy,style,0,vx,vy}: muzzle offset from the plane, S1 art style, velocity.
# Index per role: {first,n} for rot 0 and for rot 1 (FANX / RING2 / CURTAIN alternate).
fs,ls=fire['small_speed'],fire['large_speed'];ta=triple['anchor']
ev={'FANX':[[(ta[0],ta[1],1,vx,vy) for vx,vy in rc['fan5']],[(ta[0],ta[1],1,vx,vy) for vx,vy in rc['fan4']]],'TRIPLE':[[(ta[0],ta[1],1,0,fs)]]*2,
 'THREE':[[(18,66,0,*rc['three'][0])]+[(18,60,2,*v) for v in rc['three'][1:]]]*2,'BLUE3':[[(18,66,2,*v) for v in rc['blue3']]]*2,
 'RING2':[[(18,26,2,*v) for v in ring[:12]],[(18,26,2,*v) for v in ring[12:]]],
 'CURTAIN':[[(d,130,3,0,ls) for d in rc['curtain_dx'][0]],[(d,130,3,0,ls) for d in rc['curtain_dx'][1]]]}
# GHD5: explicit triple flag per row = GHD4 M rule (style 1, or a style of the tier mask); the same for every tier
tm=c['density_tiers']['triple_styles'];assert len(set(tm))==1, 'GHD5: triple rows are baked from the tier mask'
ev={n:[[(dx,dy,s,1 if (s==1 or (tm[0]>>s)&1) else 0,vx,vy) for dx,dy,s,vx,vy in r] for r in rr] for n,rr in ev.items()}
# GHDA: angle rows (flag bit1) take the firing plane's oblique angle (rich.angle_deg by instance index % 3) at rich.angle_speed
aspd=rc['angle_speed'];adeg=rc['angle_deg'];assert len(adeg)==3
for n,rws in rc['angle_rows'].items():
 for k in range(len(ev[n])):
  idx=rws[k] if k<len(rws) else rws[0];row=list(ev[n][k])
  for j in idx:
   dx,dy,s_,fl,vx,vy=row[j];assert vx!=0 and abs(math.hypot(vx,vy)-aspd)<=2,(n,k,j,vx,vy);row[j]=(dx,dy,s_,fl|2,vx,vy)
  ev[n][k]=row
rich_angle=[(round(aspd*math.sin(math.radians(a))),round(aspd*math.cos(math.radians(a)))) for a in adeg]
fang=[[(round(sp*math.sin(math.radians(a))),round(sp*math.cos(math.radians(a)))) for a in fire['angle_deg']] for sp in (fire['small_speed'],fire['large_speed'])]
rows=[];index=[[0,0,0,0]]
for n in rc['roles']:
 ix=[]
 for r in ev[n]:
  hit=[i for i in range(len(rows)) if rows[i:i+len(r)]==r]
  if hit:ix+=[hit[0],len(r)]
  else:ix+=[len(rows),len(r)];rows+=r
 index.append(ix)
h[-2:-2]=[f'#define RICH_EVENT_ROWS {len(rows)}u','#define RICH_EVENT_VALUES {'+','.join('{%d,%d,%d,%d,%d,%d}'%v for v in rows)+'} /* GHDA: {dx,dy,style,flags (bit0 triple, bit1 angle row),vx,vy} */',
 '#define RICH_ANGLE_COUNT 3u','#define RICH_ANGLE_DEG {'+','.join(str(a)+'u' for a in adeg)+'}',
 '#define RICH_ANGLE_VXVY {'+','.join('{%d,%d}'%v for v in rich_angle)+'} /* GHDA: |v| = rich.angle_speed at 30/45/60 deg from straight down */',
 '#define FIRE_ANGLE_VXVY {'+','.join('{'+','.join('{%d,%d}'%v for v in sp)+'}' for sp in fang)+'} /* GHDA: light fan side shots, [small,large][angle] */',
 f"#define ANGLES_ENABLED {int(c['angles_enabled'])}u",
 '#define RICH_EVENT_INDEX {'+','.join('{'+','.join(str(x)+'u' for x in i)+'}' for i in index)+'}']
ms=c['missiles']; mt=ms['types']; assert ms['capacity']==8 and ms['per_type_capacity']==4 and len(mt)==2
for m in mt:
 assert m['height']==32 and m['storage_width']==16 and m['width'] in (8,12) and lengths[m['color_id']]==1024 and lengths[m['mask_id']]==128
h[-2:-2]=['#define MISSILE_CAPACITY 8u','#define MISSILE_TYPE_CAPACITY 4u','#define MISSILE_REQUIRED_ASSETS 0x0F000000u','#define MISSILE_SHARED_PERIOD_MS 250u', '#define MISSILE_WIDTH_VALUES {'+','.join(str(m['width'])+'u' for m in mt)+'}', '#define MISSILE_SPEED_VALUES {'+','.join(str(m['speed'])+'u' for m in mt)+'}', '#define MISSILE_EMITTER_MS_VALUES {'+','.join(str(m['emitter_period_ms'])+'u' for m in mt)+'}', '#define MISSILE_TYPE_MS_VALUES {'+','.join(str(m['type_period_ms'])+'u' for m in mt)+'}', '#define MISSILE_HITBOX_VALUES {'+','.join('{'+','.join(str(v)+'u' for v in m['hitbox'])+'}' for m in mt)+'}', '#define MISSILE_CRC_VALUES {'+','.join('0x%08Xu'%m[k] for m in mt for k in ['color_crc32','mask_crc32'])+'}']
h[-2:-2]=['#define BULLET_MEMBER_MAX 3u','#define BULLET_TRIPLE_MATERIAL 1u','#define BULLET_TRIPLE_STYLE 1u','#define BULLET_TRIPLE_ANCHOR_X 19u','#define BULLET_TRIPLE_ANCHOR_Y 57u','#define BULLET_TRIPLE_DX_VALUES {0,9,17}','#define BULLET_TRIPLE_DY_VALUES {0,5,0}','#define BULLET_TRIPLE_WIDTH 25u','#define BULLET_TRIPLE_HEIGHT 17u']
v=['// Generated by tools/generate_enemy_config.py.']
def function(name,values):
 v.extend([f'function [31:0] {name};',' input [31:0] id;',' begin case(id)'])
 v.extend(f"  32'd{i}: {name}=32'h{n:08x};" for i,n in values)
 v.extend([f"  default: {name}=0;",' endcase end','endfunction'])
function('fixed_asset_base',enumerate(bases));function('fixed_asset_length',enumerate(lengths))
function('fixed_material_color',((i,bases[m['color_id']]) for i,m in enumerate(mats)))
function('fixed_material_mask',((i,bases[m['mask_id']]) for i,m in enumerate(mats)))
function('fixed_material_size',((i,m['width']) for i,m in enumerate(mats)))
function('fixed_bullet_color',((i,bases[m['color_id']]) for i,m in enumerate(bullets+mt)))
function('fixed_bullet_mask',((i,bases[m['mask_id']]) for i,m in enumerate(bullets+mt)))
function('fixed_effect_color',((i,bases[e['color_id']]) for i,e in enumerate(effects)))
function('fixed_effect_alpha',((i,bases[e['alpha_id']]) for i,e in enumerate(effects)))
function('fixed_effect_size',((i,e['size']) for i,e in enumerate(effects)))
function('fixed_effect_frames',((i,e['frames']) for i,e in enumerate(effects)))
vh=['// Generated; edit config/enemy_scene.json.','`ifndef ENEMY_CONFIG_VH','`define ENEMY_CONFIG_VH',f'`define ENEMY_CAPACITY {cap}',f'`define ASSET_ID_MAX {len(assets)}',
 f"`define ASSET_SUPPORTED_MASK 32'h{sum(1<<a['id'] for a in assets if a['id']<32):08x}",f"`define ASSET_SUPPORTED_MASK_HI 32'h{sum(1<<(a['id']-32) for a in assets if a['id']>=32):08x}",f'`define BULLET_CAPACITY {bcap}']
vh+=[f'`define PLAYER_{k.upper()} {val}' for k,val in player.items()]
vh+=[f'`define BULLET_DRAW_WIDTH {draw["width"]}',f'`define BULLET_DRAW_HEIGHT {draw["height"]}',f'`define BULLET_MEMBER_DX {draw["member_dx"]}']
vh+=[f'`define PLAYER_BULLET_CAPACITY {pcap}',f'`define PLAYER_BULLET_MATERIAL_TYPE {ptype}',f'`define EFFECT_CAPACITY {fxcap}','`endif','']
vh[-2:-2]=['`define MISSILE_CAPACITY 8']
vh[-2:-2]=[f'`define SCENE_BANK_WORDS {bank_words}'];vh[-2:-2]=['`define BULLET_TRIPLE_STYLE 1','`define BULLET_TRIPLE_DX1 9','`define BULLET_TRIPLE_DX2 17','`define BULLET_TRIPLE_DY1 5','`define BULLET_TRIPLE_WIDTH 25','`define BULLET_TRIPLE_HEIGHT 17']
# BGT-B: per-role quota floors (RICH roles 1..6; role 0 = by material has no floor)
ts=c['triple_styles'];assert ts['mask'] in (2,3,6,7) and ts['alt_style'] in (0,2) and (ts['mask']>>ts['alt_style'])&1 and len(ts['alt_offsets'])==3
ao,asp=ts['alt_offsets'],ts['alt_span'];assert ao[0]==[0,0] and ao[2][1]==0 and 0<ao[1][0]<ao[2][0]<=23 and 0<=ao[1][1]<=15 and asp==[ao[2][0]+8,ao[1][1]+12] and asp[1]<=31
def tri(s):return (ao,asp) if s==ts['alt_style'] else (triple['offsets'],triple['span'])
h[-2:-2]=[f"#define BULLET_TRIPLE_STYLE_MASK {ts['mask']}u /* GHD5: styles allowed to use the triple layout */",
 '#define BULLET_TRIPLE_DX_BY_STYLE {'+','.join('{'+','.join(str(o[0]) for o in tri(s)[0])+'}' for s in range(4))+'}',
 '#define BULLET_TRIPLE_DY_BY_STYLE {'+','.join('{'+','.join(str(o[1]) for o in tri(s)[0])+'}' for s in range(4))+'}',
 '#define BULLET_TRIPLE_W_BY_STYLE {'+','.join(str(tri(s)[1][0])+'u' for s in range(4))+'}',
 '#define BULLET_TRIPLE_H_BY_STYLE {'+','.join(str(tri(s)[1][1])+'u' for s in range(4))+'}',
 f"#define RICH_COLOUR_BY_AIRCRAFT {ts['colour_by_aircraft']}u /* GHD5: pressure bullet style = firing aircraft material colour */"]
# GHD5R: 16-direction rotation LUT per shape (0 = V of styles 0/1, 1 = triangle of the alt style); see the GHD5R notes in the config
import math
def rot_lut(offs):
    cen=[(o[0]+3.5,o[1]+5.5) for o in offs];cx=sum(c[0] for c in cen)/3;cy=sum(c[1] for c in cen)/3;T=[];MN=[];Wd=[];Hd=[]
    for d in range(16):
        a=d*math.pi/8;ca,sa=math.cos(a),math.sin(a);t=[]
        for px,py in cen:
            dx,dy=px-cx,py-cy;t.append((math.floor(cx+ca*dx+sa*dy-3.5+0.5+1e-9),math.floor(cy-sa*dx+ca*dy-5.5+0.5+1e-9)))
        mx=min(v[0] for v in t);my=min(v[1] for v in t);T.append(t);MN.append((mx,my));Wd.append(max(v[0] for v in t)-mx+8);Hd.append(max(v[1] for v in t)-my+12)
    assert t and T[0]==[tuple(o) for o in offs], 'direction 0 must equal the GHD5S layout'
    return T,MN,Wd,Hd
ROT=[rot_lut(triple['offsets']),rot_lut(ao)]
WMAX=max(max(r[2]) for r in ROT);HMAX=max(max(r[3]) for r in ROT)
assert WMAX<=31 and HMAX<=31 and all(0<=q-m<=31 for r in ROT for t,mn in zip(r[0],r[1]) for p in t for q,m in zip(p,mn))
h[-2:-2]=['#define BULLET_TRI_ROT_DIRS 16u /* GHD5R: triple groups turn with the motion direction */',
 '#define BULLET_TRI_ROT_T {'+','.join('{'+','.join('{'+','.join('{%d,%d}'%p for p in t)+'}' for t in r[0])+'}' for r in ROT)+'}',
 '#define BULLET_TRI_ROT_MIN {'+','.join('{'+','.join('{%d,%d}'%m for m in r[1])+'}' for r in ROT)+'}',
 '#define BULLET_TRI_ROT_W {'+','.join('{'+','.join(str(w)+'u' for w in r[2])+'}' for r in ROT)+'}',
 '#define BULLET_TRI_ROT_H {'+','.join('{'+','.join(str(x)+'u' for x in r[3])+'}' for r in ROT)+'}',
 f'#define BULLET_TRI_ROT_WMAX {WMAX}u',f'#define BULLET_TRI_ROT_HMAX {HMAX}u',f"#define BULLET_TRI_ALT_STYLE {ts['alt_style']}u"]
vh[-2:-2]=['`define BULLET_TRI_ROT 1',f'`define BULLET_TRI_ROT_WMAX {WMAX}',f'`define BULLET_TRI_ROT_HMAX {HMAX}']
def vfun(name,width,vals):  # vals: dict key(int)->value
    return [f'function [{width-1}:0] {name};',' input [6:0] k;',' begin case(k)']+[f"  7'd{k}: {name}={width}'d{v};" for k,v in sorted(vals.items())]+[f'  default: {name}={width}\'d0;',' endcase end','endfunction']
dxv={(s<<6)|(d<<2)|m:ROT[s][0][d][m][0]-ROT[s][1][d][0] for s in (0,1) for d in range(16) for m in range(3)}
dyv={(s<<6)|(d<<2)|m:ROT[s][0][d][m][1]-ROT[s][1][d][1] for s in (0,1) for d in range(16) for m in range(3)}
wv={(s<<6)|(d<<2):ROT[s][2][d] for s in (0,1) for d in range(16)};hv={(s<<6)|(d<<2):ROT[s][3][d] for s in (0,1) for d in range(16)}
rotvh=['// Generated by tools/generate_enemy_config.py (GHD5R). Key = {shape(1), dir(4), member(2)}; shape 1 = alt (triangle) style.']+\
 vfun('tri_rot_dx',5,dxv)+vfun('tri_rot_dy',5,dyv)+vfun('tri_rot_w',5,wv)+vfun('tri_rot_h',5,hv)
vh[-2:-2]=[f"`define BULLET_TRIPLE_STYLE_MASK 4'd{ts['mask']}",f"`define BULLET_TRIPLE_ALT_STYLE 2'd{ts['alt_style']}",
 f"`define BULLET_TRIPLE_ALT_DX1 {ao[1][0]}",f"`define BULLET_TRIPLE_ALT_DY1 {ao[1][1]}",f"`define BULLET_TRIPLE_ALT_DX2 {ao[2][0]}",
 f"`define BULLET_TRIPLE_ALT_WIDTH {asp[0]}",f"`define BULLET_TRIPLE_ALT_HEIGHT {asp[1]}"]
dt=c['density_tiers'];assert len(dt['triple_styles'])==3 and all(0<=m<16 for m in dt['triple_styles']) and 0<=dt['default']<3
assert len(dt['tag_pct'])==3 and all(1<=p<=999 for p in dt['tag_pct']) and len(dt['pool_caps'])==3 and all(64<=p<=bcap for p in dt['pool_caps']) and len(dt['period_pct'])==3 and all(50<=p<=100 for p in dt['period_pct'])
h[-2:-2]=['#define BULLET_DENSITY_TRIPLE_STYLES {'+','.join(str(m)+'u' for m in dt['triple_styles'])+'} /* GHD4: tier style mask (all M) */',
          '#define BULLET_DENSITY_POOL_CAPS {'+','.join(str(p)+'u' for p in dt['pool_caps'])+'} /* GHD4: pressure pool cap per tier */',
          '#define BULLET_DENSITY_PERIOD_PCT {'+','.join(str(p)+'u' for p in dt['period_pct'])+'} /* GHD4: pressure emission period % per tier */',
          '#define BULLET_DENSITY_PERIOD_PERMILLE {'+','.join(str(p)+'u' for p in dt.get('period_permille',[q*10 for q in dt['period_pct']]))+'} /* GHDA: period per mille per tier */',
          '#define BULLET_DENSITY_GLOW {'+','.join(str(p)+'u' for p in dt.get('glow',[0,0,0]))+'} /* GHDA: extra pressure glow shots per tier */',
          '#define BULLET_DENSITY_TAG_PCT {'+','.join(str(p)+'u' for p in dt['tag_pct'])+'} /* GHD4: HUD tier tag M-nnn */',
          f"#define BULLET_DENSITY_DEFAULT {dt['default']}u"]
# GHDA probe ladder (pressure test tier, telemetry)
pl=c['probe_ladder'];lv=pl['levels'];hudc=' 0123456789ABCDEFGHIJKLMNOPRSTUVWXY/:.-x'
assert 1<=len(lv)<=16 and 5<=pl['level_seconds']<=600 and len(pl['tier_tag'])<=5 and all(ch in hudc for ch in pl['tier_tag'])
for l in lv: assert len(l['tag'])<=5 and all(ch in hudc for ch in l['tag']) and 64<=l['pool']<=bcap and 500<=l['period_permille']<=1000 and l['angles'] in (0,1) and 0<=l['glow']<=4, l
h[-2:-2]=[f"#define LADDER_TIER_TAG \"{pl['tier_tag']}\"",f"#define LADDER_LEVEL_COUNT {len(lv)}u",f"#define LADDER_LEVEL_SECONDS {pl['level_seconds']}u",
          '#define LADDER_TAGS {'+','.join('"'+l['tag']+'"' for l in lv)+'}','#define LADDER_POOL {'+','.join(str(l['pool'])+'u' for l in lv)+'}',
          '#define LADDER_PERIOD_PERMILLE {'+','.join(str(l['period_permille'])+'u' for l in lv)+'}','#define LADDER_ANGLES {'+','.join(str(l['angles'])+'u' for l in lv)+'}',
          '#define LADDER_GLOW {'+','.join(str(l['glow'])+'u' for l in lv)+'}',f"#define PROBE_LADDER_ENABLED {int(pl.get('enabled',1))}u",
          '#define BULLET_TIER_LADDER 3u','#define BULLET_TIER_COUNT (3u+PROBE_LADDER_ENABLED)']
ld=c['light_density'];assert 25<=ld['period_pct']<=100, 'light period pct'
h[-2:-2]=[f"#define BULLET_LIGHT_PERIOD_PCT {ld['period_pct']}u /* GHD5: LIGHT fire period % (pattern 0/1/2) */"]
rq=c['rich_quota'];assert sorted(rq['per_plane'])==sorted(c['rich']['roles']) and 0<rq['shared_min']<bcap
h[-2:-2]=['#define RICH_QUOTA_PER_PLANE_VALUES {0u,'+','.join(str(rq['per_plane'][n])+'u' for n in c['rich']['roles'])+'}',f"#define RICH_QUOTA_SHARED_MIN {rq['shared_min']}u"]
# BGT-A (bg_turret_overnight_20261028): I4 background (ID bg) + palette, 32x32 RGB565LE+A4 decor skins, 3 turrets x 2 muzzles.
bgi=c['background_i4'];dc=c['decor']
assert bgi['width']==1920 and bgi['height']==1080 and bgi['stride']==960 and lengths[bgi['id']]==1920*1080//2 and lengths[bgi['palette_id']]==128
assert dc['size']==32 and dc['color_stride']==64 and dc['alpha_offset']==2048 and dc['alpha_stride']==16 and dc['slots'] in (8,16) and len(dc['skins'])==2
assert all(lengths[i]==2560 and bases[i]%128==0 for i in dc['skins']) and 1<=dc['max_active']<=dc['slots'] and dc['tick_hz']==60
r2=dc['rate_2x'];assert 1<=r2['max_active']<=dc['slots'] and len(r2['first_ticks'])==3 and all(0<=f<r2['period_ticks'] for f in r2['first_ticks']) and 0<r2['period_ticks']<dc['period_ticks']
mi=c['menu_i4'];assert mi['width']==1920 and mi['height']==1080 and mi['stride']==960 and mi['even_x']=='high' and lengths[mi['id']]==1920*1080//2 and lengths[mi['palette_id']]==128 and mi['id']>=32 and mi['palette_id']>=32
assert len(dc['turrets'])==3 and all(len(t['muzzles'])==2 and t['skin'] in (0,1) for t in dc['turrets']) and len(dc['first_ticks'])==3
for t in dc['turrets']:
 for mx,my in t['muzzles']:assert 0<=mx<1920 and 0<=my<1080
h[-2:-2]=[f"#define BG_I4_ID {bgi['id']}u",f"#define BG_PALETTE_ID {bgi['palette_id']}u",
 f"#define BG_I4_REQUIRED_ASSETS 0x{(1<<bgi['id'])|(1<<bgi['palette_id']):08X}u",
 '#define DECOR_SKIN_IDS {'+','.join(f'{i}u' for i in dc['skins'])+'}',f"#define DECOR_SLOTS {dc['slots']}u",f"#define DECOR_MAX_ACTIVE {dc['max_active']}u",
 f"#define DECOR_SIZE {dc['size']}u",f"#define DECOR_PERIOD_TICKS {dc['period_ticks']}u",f"#define DECOR_LIFE_TICKS {dc['life_ticks']}u",
 '#define DECOR_FIRST_TICKS {'+','.join(f'{v}u' for v in dc['first_ticks'])+'}',
 '#define DECOR_RECYCLE_CENTER {'+','.join(str(v) for v in dc['recycle_center'])+'}',
 '#define DECOR_TURRET_COUNT 3u',
 '#define DECOR_MUZZLE_VALUES {'+','.join('{'+','.join(f'{{{x},{y}}}' for x,y in t['muzzles'])+'}' for t in dc['turrets'])+'}',
 '#define DECOR_VELOCITY_VALUES {'+','.join(f"{{{t['v'][0]},{t['v'][1]}}}" for t in dc['turrets'])+'}',
 '#define DECOR_SKIN_VALUES {'+','.join(f"{t['skin']}u" for t in dc['turrets'])+'}',
 f"#define DECOR_PERIOD_TICKS_2X {r2['period_ticks']}u",'#define DECOR_FIRST_TICKS_2X {'+','.join(f'{v}u' for v in r2['first_ticks'])+'}',f"#define DECOR_MAX_ACTIVE_2X {r2['max_active']}u",
 f"#define MENU_I4_ID {mi['id']}u",f"#define MENU_PALETTE_ID {mi['palette_id']}u",f"#define MENU_REQUIRED_ASSETS_HI 0x{(1<<(mi['id']-32))|(1<<(mi['palette_id']-32)):08X}u"]
vh[-2:-2]=[f"`define BG_I4_BASE 32'h{bases[bgi['id']]:08x}",f"`define BG_PALETTE_BASE 32'h{bases[bgi['palette_id']]:08x}",
 f"`define DECOR_BASE0 32'h{bases[dc['skins'][0]]:08x}",f"`define DECOR_BASE1 32'h{bases[dc['skins'][1]]:08x}",f"`define DECOR_SLOTS {dc['slots']}",
 f"`define MENU_I4_BASE 32'h{bases[mi['id']]:08x}",f"`define MENU_PALETTE_BASE 32'h{bases[mi['palette_id']]:08x}"]
outputs={R/'enemy_tri_rot.vh':'\n'.join(rotvh)+'\n',R/'embedded_sw/riscv_soc/software/standalone/apbTest/src/enemy_config.h':'\n'.join(h),R/'enemy_config.vh':'\n'.join(vh),R/'enemy_catalog.vh':'\n'.join(v)+'\n'}
for p,s in outputs.items():
 if '--check' in sys.argv:assert p.read_text()==s,str(p)
 else:p.write_text(s,encoding='utf-8')
print('Capacity/catalog consistent:',cap,'enemies,',bcap,'bullets,',fxcap,'effects,',len(assets),'resources,',', '.join(f'{n}={len(waves[n])}' for n in wave_names))
