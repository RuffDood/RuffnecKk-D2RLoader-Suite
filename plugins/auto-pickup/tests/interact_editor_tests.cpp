#include "../src/interact_editor.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if(!(x)) {std::cerr<<__LINE__<<": " #x "\n";std::exit(1);} } while(0)
namespace S=Reimagined::InteractSettings;
S::Values current; bool refused{},available=true;
auto Read(S::Values* out) noexcept -> S::Result {if(!available)return S::Result::Unavailable;*out=current;return S::Result::Success;}
auto Save(const S::Values* expected,const S::Values* draft,char* error,std::uint32_t size) noexcept -> S::Result {
    if(refused || !S::Same(*expected,current)) {snprintf(error,size,"Save refused");return S::Result::Conflict;}current=*draft;return S::Result::Success;
}
int main() {
    S::Api api{sizeof(S::Api),S::Version,Read,Save};S::Editor editor;editor.Open();CHECK(!editor.view.available);
    editor.Bind(&api);editor.Open();CHECK(editor.view.available);editor.Number(0,20);editor.Toggle(2,false);CHECK(editor.view.dirty);
    refused=true;editor.Save();CHECK(editor.view.dirty);refused=false;editor.Save();CHECK(!editor.view.dirty && current.range==20 && !(current.flags&S::Corpses));
    S::Editor other;other.Bind(&api);other.Open();other.Number(0,4);other.Save();editor.Number(0,5);editor.Save();CHECK(editor.view.dirty);
    available=false;editor.Open();CHECK(!editor.view.available);available=true;editor.Bind(nullptr);editor.Open();CHECK(!editor.view.available);
}
