#!/usr/bin/env python3
"""Execute the production OLED lambda against ESPHome-generated font metrics.

The recorder replaces only the hardware drawing boundary. It checks full font
line boxes (conservatively including blank ascent/descent), using actual glyph
advances/offsets and heights from the pinned build. This catches the photographed
clipping and mixed-color lines without copying production layout decisions.
No ESPHome/Pillow import is needed; generate main.cpp before running this test.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap


root = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2:
    raise SystemExit("Usage: python3 tests/display_layout_test.py PATH_TO_GENERATED_MAIN_CPP")
source = (root / "packages/display.yaml").read_text()
body = textwrap.dedent(source.split("    lambda: |-\n", 1)[1])
substitutions = dict(re.findall(
    r'^  (\w+):\s*"?([^"\n]+)"?$',
    (root / "packages/battery-config.yaml").read_text(), re.MULTILINE,
))
body = body.replace("${oled_rotation}", "rotation")
for key, value in substitutions.items():
    body = body.replace("${" + key + "}", value)

# Font sizes are nominal; use the actual generated advances and line heights.
main = Path(sys.argv[1]).read_text()
font_code = ""
for name in ["small", "large"]:
    match = re.search(
        r'new\(display_font_' + name + r'\) font::Font\((\w+), \d+, \d+, (\d+),', main,
    )
    if match is None:
        raise SystemExit("Generate firmware C++ with the pinned ESPHome version first")
    array, height = match.groups()
    line = re.search(r'static const font::Glyph ' + array + r'\[\] = (.*);', main)[1]
    glyphs = re.findall(
        r'\{(\d+), \([^)]*\), (\d+), (-?\d+), (-?\d+), (\d+), (\d+)\}', line,
    )
    entries = ["{" + glyph[0] + ", {" + ",".join(glyph[1:]) + "}}" for glyph in glyphs]
    font_code += f"Font {name}{{{height}, {{" + ",".join(entries) + "}};\n"

cpp=r'''
#define BATTERY_MONITOR_HOST_TEST
#include "include/battery_monitor_types.h"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
struct Glyph {int advance,x,y,w,h;};
struct Font {int height; std::map<int,Glyph> glyphs;};
FONT_CODE
Font *display_font_small=&small, *display_font_large=&large;
struct State {float state;};
State measurement_healthy{1}, battery_current{500.02}, battery_voltage{0.81}, battery_power{406},device_online{0},rated_capacity_ah{300};
bool soc_valid_live=false, soc_rule_engine_ready_live=false;
float soc_remaining_ah_live=150;
uint32_t soc_rule_latches[3]={};
uint32_t now=0;
uint32_t millis(){return now;}
#define id(x) x
enum class TextAlign {TOP_LEFT,TOP_CENTER};
struct Box{int x,y,w,h; std::string text;};
struct Display {
 std::vector<Box> boxes;
 int get_width(){return 128;}
 void get_text_bounds(int x,int y,const char* t,Font*f,TextAlign a,int*x1,int*y1,int*w,int*h){
  int advance=0, offset=0;bool first=true;
  for(const char*c=t;*c;++c){auto g=f->glyphs.at(*c);if(first){offset=g.x;first=false;}else offset=std::min(offset,advance+g.x);advance+=g.advance;}
  *w=advance-offset;*h=f->height;*x1=a==TextAlign::TOP_CENTER?x-advance/2:x;*y1=y;
 }
 void print(int x,int y,Font*f,TextAlign a,const char*t){
  int x1,y1,w,h;get_text_bounds(x,y,t,f,a,&x1,&y1,&w,&h);
  boxes.push_back({x1,y1,w,h,t});
 }
 void print(int x,int y,Font*f,const char*t){print(x,y,f,TextAlign::TOP_LEFT,t);}
 void printf(int x,int y,Font*f,TextAlign a,const char*fmt,...){char s[256];va_list args;va_start(args,fmt);vsnprintf(s,sizeof(s),fmt,args);va_end(args);print(x,y,f,a,s);}
 void printf(int x,int y,Font*f,const char*fmt,...){char s[256];va_list args;va_start(args,fmt);vsnprintf(s,sizeof(s),fmt,args);va_end(args);print(x,y,f,s);}
};
void draw(Display &it, int rotation){ BODY }
int main(){int failures=0;
 for(int rotation : {0,180})for(int scenario=0;scenario<9;scenario++)for(int page=0;page<3;page++){
  now=page*5000;measurement_healthy.state=scenario!=0;soc_valid_live=scenario>1;
  soc_rule_engine_ready_live=scenario>2;device_online.state=scenario>1;
  soc_remaining_ah_live=scenario==4?-330:scenario==5?3300:scenario==6?30000000:150;
  battery_current.state=scenario==3?-12.5f:scenario==7?-500.02f:500.02f;battery_power.state=scenario==3?-165:scenario==7?-16000:406;
  battery_voltage.state=scenario==3?13.2f:0.81f;
  if(scenario==8)soc_remaining_ah_live=-1e30f;
  if(scenario==3)soc_remaining_ah_live=75;
  battery_monitor::reset_rule_latches(soc_rule_latches);
  if(scenario==3){battery_monitor::set_rule_latch(soc_rule_latches,battery_monitor::RULE_LATCH_STOP_LOAD_FLAG,true);battery_monitor::set_rule_latch(soc_rule_latches,battery_monitor::RULE_LATCH_CAPACITY_WARNING_FLAG,true);}
  Display d;draw(d, rotation);
  const int boundary=rotation==0?16:48;
  int status_lines=0, body_lines=0;
  for(const auto &box:d.boxes) {
    if(rotation==0 ? box.y<16 : box.y>=48) ++status_lines;
    else ++body_lines;
  }
  if(status_lines!=1 || body_lines==0) {
    std::cerr<<"FAIL missing body or single status line "<<scenario<<"/"<<page<<"\n";
    ++failures;
  }
  for(auto &b:d.boxes){
   bool bad=b.x<0 || b.x+b.w>128 || b.y<0 || b.y+b.h>64 || (b.y<boundary && b.y+b.h>boundary);
   if(bad){std::cerr<<"FAIL "<<scenario<<"/"<<page<<" bounds "<<b.x<<","<<b.y<<","<<b.w<<","<<b.h<<" "<<b.text<<"\n";++failures;}
  }
  for(size_t i=0;i<d.boxes.size();i++)for(size_t j=i+1;j<d.boxes.size();j++){
   auto a=d.boxes[i],b=d.boxes[j];if(a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h){std::cerr<<"FAIL overlap "<<scenario<<"/"<<page<<" "<<a.text<<" / "<<b.text<<"\n";++failures;}
  }
 }
 return failures?1:0;
}
'''.replace('FONT_CODE',font_code).replace('BODY',body)
with tempfile.TemporaryDirectory(prefix="oled-layout-") as directory:
    source = Path(directory) / "layout.cpp"
    binary = Path(directory) / "layout"
    source.write_text(cpp)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                    "-Werror", "-I"+str(root), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Display geometry passed: 54 states/orientations, no clipping, overlap, or color-band crossings")
