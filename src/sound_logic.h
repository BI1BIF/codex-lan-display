#pragma once
#include <stdint.h>
namespace sound_logic {
enum class State : uint8_t { Unknown, Running, Waiting, Done, Idle, Error };
enum class Cue : uint8_t { None, Running, Question, Done };
struct Transitions {
 State previous=State::Unknown;
 bool armed=false;
 constexpr Cue update(State next){
  if(next==previous)return Cue::None;
  previous=next;
  if(next==State::Unknown){armed=false;return Cue::None;}
  if(next==State::Running){armed=true;return Cue::Running;}
  if(next==State::Waiting){armed=true;return Cue::Question;}
  if(next==State::Done){bool play=armed;armed=false;return play?Cue::Done:Cue::None;}
  armed=false;return Cue::None;
 }
};
constexpr bool selfTest(){
 Transitions t;
 if(t.update(State::Done)!=Cue::None)return false;
 if(t.update(State::Running)!=Cue::Running)return false;
 if(t.update(State::Running)!=Cue::None)return false;
 if(t.update(State::Waiting)!=Cue::Question)return false;
 if(t.update(State::Waiting)!=Cue::None)return false;
 if(t.update(State::Done)!=Cue::Done)return false;
 if(t.update(State::Done)!=Cue::None)return false;
 t.update(State::Running);t.update(State::Unknown);
 if(t.update(State::Done)!=Cue::None)return false;
 t.update(State::Running);t.update(State::Error);
 if(t.update(State::Done)!=Cue::None)return false;
 t.update(State::Running);t.update(State::Idle);
 return t.update(State::Done)==Cue::None;
}
static_assert(selfTest(),"Audio must sound once per transition and never invent completion");
}
