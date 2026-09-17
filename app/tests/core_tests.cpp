#include "core.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>
class MemoryCatalog:public books::CatalogPort{public:Json::Value data;Json::Value load()const override{return data;}};
void check(bool condition,const char*why){if(!condition)throw std::runtime_error(why);}
int main(){
 auto dir=std::filesystem::temp_directory_path()/("vk-search-test-"+std::to_string(getpid()));std::filesystem::create_directory(dir);
 try{
 MemoryCatalog c;c.data["items"]=Json::arrayValue;
 Json::Value b;b["community"]="proglib";b["id"]="1_1";b["title"]="Ёлка и C++";b["post_text"]="Объявление";b["annotation"]="учебник";b["authors"]=Json::arrayValue;b["authors"].append("Иванов");b["tags"]=Json::arrayValue;b["tags"].append("C++");b["publication_year"]=2024;c.data["items"].append(b);
 b["community"]="bookflow";b["id"]="1_2";b["title"]="Другая книга";b["publication_year"]=Json::nullValue;b["authors"]=Json::arrayValue;c.data["items"].append(b);
 std::ofstream(dir/"1_1.txt")<<"Уникальная фраза только внутри документа";
 books::SearchService s(c,dir);books::Query q;q.title="ЕЛКА";check(s.search(q)["total"].asInt()==1,"Russian casefold and yo");
 q={};q.text="уникальная фраза";q.scope="body";auto r=s.search(q);check(r["total"].asInt()==1,"Body phrase found");check(r["items"][0]["snippet"].asString().find("уникальная")!=std::string::npos,"Snippet present");
 q.scope="title";check(s.search(q)["total"].asInt()==0,"Scope isolation");
 q={};q.author="иванов";q.year=2024;check(s.search(q)["total"].asInt()==1,"Combined filters exclude unknown");
 q={};q.text="\" OR *";check(s.search(q)["total"].asInt()==0,"FTS input treated as phrase");
 q={};q.limit=1;q.offset=1;r=s.search(q);check(r["total"].asInt()==2&&r["items"].size()==1,"Pagination total");
 q={};q.limit=0;bool rejected=false;try{s.search(q);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid limit");
 q={};q.title="елка";q.titleMatch="exact";check(s.search(q)["total"].asInt()==0,"Exact not partial");
 q={};q.scope="body OR 1=1";rejected=false;try{s.search(q);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Scope injection rejected");
 q={};q.community="PROGLIB";check(s.search(q)["total"].asInt()==1,"Community casefold");q.title="Другая";check(s.search(q)["total"].asInt()==0,"Community and title intersect");
 books::writeJson(dir/"catalog.json",c.data);Json::Value metadata;metadata["1_1"]["authors"]=Json::arrayValue;metadata["1_1"]["authors"].append("Подтверждённый автор");metadata["1_1"]["source_url"]="https://invalid.example";books::writeJson(dir/"metadata.json",metadata);
 books::JsonCatalog persisted(dir/"catalog.json");check(persisted.load()["items"][0]["authors"][0].asString()=="Подтверждённый автор","Metadata enrichment");
 books::writeJson(dir/"catalog.json",c.data);check(persisted.load()["items"][0]["authors"][0].asString()=="Подтверждённый автор","Enrichment survives raw catalog refresh");check(!persisted.load()["items"][0].isMember("source_url"),"Source cannot be overridden");
 for(const auto *ext:{"pdf","DJVU","doc"})check(books::supportedDocumentFormat(ext),"Required VK document format accepted");
 for(const auto *ext:{"gif","rar","","pdf.exe"})check(!books::supportedDocumentFormat(ext),"Unrelated attachment rejected");
 std::filesystem::remove_all(dir);std::cout<<"14 search contract checks and 7 document format checks passed\n";
 }catch(const std::exception&e){std::filesystem::remove_all(dir);std::cerr<<e.what()<<'\n';return 1;}
}
