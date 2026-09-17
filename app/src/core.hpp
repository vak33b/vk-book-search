#pragma once
#include <json/json.h>
#include <filesystem>
#include <string>
#include <vector>
namespace books {
namespace fs=std::filesystem;
std::string normalize(const std::string &s);
bool supportedDocumentFormat(const std::string &extension);
Json::Value readJson(const fs::path &p);
void writeJson(const fs::path &p,const Json::Value &v);
struct Query { std::string text,title,author,tag,community,scope="all",titleMatch="partial"; int year=0,limit=20,offset=0; };
class CatalogPort {public: virtual ~CatalogPort()=default; virtual Json::Value load() const=0;};
class JsonCatalog final:public CatalogPort {fs::path path_;public: explicit JsonCatalog(fs::path p):path_(std::move(p)){} Json::Value load() const override;};
class SearchService {const CatalogPort &catalog_; fs::path textDir_;public: SearchService(const CatalogPort &c,fs::path textDir):catalog_(c),textDir_(std::move(textDir)){} Json::Value search(const Query &q) const;};
class VkSource {std::string token_; public:explicit VkSource(const fs::path &env); Json::Value request(const std::string &method,const std::string &domain,int offset=0) const;};
Json::Value sync(const fs::path &root);
}
