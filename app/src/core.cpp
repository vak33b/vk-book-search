#include "core.hpp"
#include <curl/curl.h>
#include <unicode/unistr.h>
#include <unicode/normalizer2.h>
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <memory>
namespace books {
std::string normalize(const std::string &s){
 UErrorCode err=U_ZERO_ERROR;auto n=icu::Normalizer2::getNFKCCasefoldInstance(err);icu::UnicodeString out;
 n->normalize(icu::UnicodeString::fromUTF8(s),out,err);if(U_FAILURE(err))throw std::runtime_error("Unicode normalization failed");
 out.findAndReplace(icu::UnicodeString::fromUTF8("ё"),icu::UnicodeString::fromUTF8("е"));std::string r;out.toUTF8String(r);return r;
}
Json::Value readJson(const fs::path &p){std::ifstream f(p);if(!f)throw std::runtime_error("Cannot open JSON file");Json::Value v;Json::CharReaderBuilder b;std::string e;if(!Json::parseFromStream(b,f,&v,&e))throw std::runtime_error("Invalid JSON file");return v;}
void writeJson(const fs::path &p,const Json::Value &v){fs::create_directories(p.parent_path());auto tmp=p;tmp+=".tmp";std::ofstream f(tmp);Json::StreamWriterBuilder b;b["indentation"]="  ";f<<Json::writeString(b,v)<<'\n';f.close();if(!f)throw std::runtime_error("Cannot write catalog");fs::rename(tmp,p);}
Json::Value JsonCatalog::load()const{
 auto catalog=readJson(path_);auto metadataPath=path_.parent_path()/"metadata.json";
 if(!fs::exists(metadataPath))return catalog;
 auto metadata=readJson(metadataPath);
 for(auto&book:catalog["items"]){auto id=book["id"].asString();if(!metadata.isMember(id))continue;const auto&m=metadata[id];
  for(const auto*field:{"authors","tags"})if(m.isMember(field)){
   if(!m[field].isArray())throw std::runtime_error("Invalid metadata list");
   for(const auto&value:m[field])if(!value.isString())throw std::runtime_error("Invalid metadata value");
   book[field]=m[field];
  }
  if(m["annotation"].isString())book["annotation"]=m["annotation"];
  if(m["publication_year"].isInt()&&m["publication_year"].asInt()>0&&m["publication_year"].asInt()<=9999)book["publication_year"]=m["publication_year"];
  book["metadata_provenance"]=m["provenance"];
 }
 return catalog;
}
static bool contains(const std::string&a,const std::string&b){return normalize(a).find(normalize(b))!=std::string::npos;}
static std::string loadText(const fs::path &p){std::ifstream f(p);return f?std::string(std::istreambuf_iterator<char>(f),{}):"";}
// A request-local FTS index keeps this first prototype simple and thread isolated.
// A persistent index is needed when the corpus grows.
Json::Value SearchService::search(const Query &q)const{
 if(q.limit<1||q.limit>100||q.offset<0||q.offset>100000||q.year<0||q.year>9999||q.text.size()>1024||q.title.size()>1024||q.author.size()>1024||q.tag.size()>256||q.community.size()>256)throw std::invalid_argument("Invalid search parameters");
 if(q.scope!="all"&&q.scope!="title"&&q.scope!="post"&&q.scope!="body"&&q.scope!="annotation")throw std::invalid_argument("Invalid scope");
 if(q.titleMatch!="partial"&&q.titleMatch!="exact")throw std::invalid_argument("Invalid title_match");
 auto catalog=catalog_.load();if(!catalog["items"].isArray())throw std::runtime_error("Invalid catalog schema");
 sqlite3* raw=nullptr;if(sqlite3_open(":memory:",&raw)!=SQLITE_OK){if(raw)sqlite3_close(raw);throw std::runtime_error("Index open failed");}
 std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
 if(sqlite3_exec(raw,"CREATE VIRTUAL TABLE corpus USING fts5(title,post,annotation,body)",nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error("FTS5 unavailable");
 auto prepare=[&](const char*sql){sqlite3_stmt *s=nullptr;if(sqlite3_prepare_v2(raw,sql,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("Index query failed");return std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>(s,sqlite3_finalize);};
 auto ins=prepare("INSERT INTO corpus(rowid,title,post,annotation,body) VALUES(?,?,?,?,?)");
 int index=0,indexed=0;
 for(const auto &b:catalog["items"]){++index;std::string text;
  auto id=b["id"].asString();if(id.find_first_not_of("0123456789_-.")!=std::string::npos)throw std::runtime_error("Invalid document id");
  if(fs::exists(textDir_/(id+".txt"))){text=loadText(textDir_/(id+".txt"));if(!text.empty())++indexed;}
  sqlite3_bind_int(ins.get(),1,index);std::vector<std::string> fields={b["title"].asString(),b.get("post_text","").asString(),b["annotation"].isString()?b["annotation"].asString():"",text};
  for(int i=0;i<4;++i){auto s=normalize(fields[i]);sqlite3_bind_text(ins.get(),i+2,s.c_str(),-1,SQLITE_TRANSIENT);}
  if(sqlite3_step(ins.get())!=SQLITE_DONE)throw std::runtime_error("Index insert failed");sqlite3_reset(ins.get());sqlite3_clear_bindings(ins.get());
 }
 std::map<int,std::string> hits;
 if(!q.text.empty()){
  std::string phrase="\"";for(char c:normalize(q.text)){phrase+=c;if(c=='"')phrase+='"';}phrase+='"';if(q.scope!="all")phrase=q.scope+" : "+phrase;
  auto s=prepare("SELECT rowid,snippet(corpus,-1,'[',']',' … ',24) FROM corpus WHERE corpus MATCH ? ORDER BY bm25(corpus)");sqlite3_bind_text(s.get(),1,phrase.c_str(),-1,SQLITE_TRANSIENT);int rc;
  while((rc=sqlite3_step(s.get()))==SQLITE_ROW)hits[sqlite3_column_int(s.get(),0)]=reinterpret_cast<const char*>(sqlite3_column_text(s.get(),1));
  if(rc!=SQLITE_DONE)throw std::invalid_argument("Unsupported query");
 }
 Json::Value result;result["items"]=Json::arrayValue;int total=0;index=0;
 for(auto b:catalog["items"]){++index;
  if(!q.text.empty()&&!hits.contains(index))continue;
  if(!q.community.empty()&&normalize(b.get("community", "").asString())!=normalize(q.community))continue;
  if(!q.title.empty()&&(q.titleMatch=="exact"?normalize(b["title"].asString())!=normalize(q.title):!contains(b["title"].asString(),q.title)))continue;
  if(q.year&&(b["publication_year"].isNull()||b["publication_year"].asInt()!=q.year))continue;
  if(!q.author.empty()){bool match=false;for(const auto&a:b["authors"])if(contains(a.asString(),q.author))match=true;if(!match)continue;}
  if(!q.tag.empty()){bool match=false;for(const auto&t:b["tags"])if(normalize(t.asString())==normalize(q.tag))match=true;if(!match)continue;}
  if(total++<q.offset)continue;if(result["items"].size()>=static_cast<unsigned>(q.limit))continue;
  if(!q.text.empty())b["snippet"]=hits[index];b["text_available"]=fs::exists(textDir_/(b["id"].asString()+".txt"));result["items"].append(b);
 }
 result["total"]=total;result["limit"]=q.limit;result["offset"]=q.offset;result["text_indexed_documents"]=indexed;result["catalog_documents"]=catalog["items"].size();result["coverage"]="Selected VK publications only; incomplete coverage";result["query_mode"]="literal_phrase";result["scope"]=q.scope;result["collected_at"]=catalog["collected_at"];return result;
}
VkSource::VkSource(const fs::path &env){std::ifstream f(env);std::string s;while(std::getline(f,s))if(s.rfind("VK_SERVICE_TOKEN=",0)==0)token_=s.substr(17);if(token_.empty()||token_.find_first_of(" \r\n\t")!=std::string::npos)throw std::runtime_error("Service token missing or malformed");}
static size_t receive(char*p,size_t a,size_t b,void*ctx){auto &s=*static_cast<std::string*>(ctx);auto n=a*b;if(s.size()+n>16*1024*1024)return 0;s.append(p,n);return n;}
Json::Value VkSource::request(const std::string&method,const std::string&domain,int offset)const{
 if(method!="wall.get"&&method!="wall.search")throw std::runtime_error("Method not allowed");
 CURL *raw=curl_easy_init();if(!raw)throw std::runtime_error("HTTP initialization failed");std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> c(raw,curl_easy_cleanup);
 auto escape=[&](const std::string&s){char*p=curl_easy_escape(raw,s.c_str(),static_cast<int>(s.size()));if(!p)throw std::runtime_error("Encoding failed");std::string r=p;curl_free(p);return r;};
 auto body="access_token="+escape(token_)+"&v=5.199&domain="+escape(domain)+"&count=100&offset="+std::to_string(offset);if(method=="wall.search")body+="&query="+escape("книг");
 auto url="https://api.vk.com/method/"+method;std::string output;
 curl_easy_setopt(raw,CURLOPT_URL,url.c_str());curl_easy_setopt(raw,CURLOPT_POSTFIELDS,body.c_str());curl_easy_setopt(raw,CURLOPT_TIMEOUT,30L);curl_easy_setopt(raw,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(raw,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(raw,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(raw,CURLOPT_WRITEDATA,&output);
 auto rc=curl_easy_perform(raw);if(rc!=CURLE_OK)throw std::runtime_error("VK transport error "+std::to_string(rc));long status=0;curl_easy_getinfo(raw,CURLINFO_RESPONSE_CODE,&status);if(status!=200)throw std::runtime_error("VK HTTP status "+std::to_string(status));
 Json::CharReaderBuilder reader;Json::Value json;std::string errors;std::istringstream in(output);if(!Json::parseFromStream(reader,in,&json,&errors))throw std::runtime_error("VK invalid JSON");if(json.isMember("error"))throw std::runtime_error("VK API error "+std::to_string(json["error"].get("error_code",0).asInt()));if(!json["response"]["items"].isArray())throw std::runtime_error("VK invalid response");return json["response"];
}
bool supportedDocumentFormat(const std::string &extension){
 const auto ext=normalize(extension);
 return ext=="pdf"||ext=="djvu"||ext=="doc"||ext=="txt"||ext=="fb2"||ext=="epub";
}
Json::Value sync(const fs::path&root){
 VkSource vk(root/".env");auto cfg=readJson(root/"config/vk-sources.example.json");std::map<std::string,Json::Value> records;Json::Value privateUrls(Json::objectValue),runs(Json::arrayValue);int skipped=0;
 // Do not replace a previous valid catalog when all remote requests fail.
 for(const auto&group:cfg["communities"]){auto domain=group["domain"].asString();for(const std::string method:{"wall.get","wall.search"}){
 int maxPages=cfg.get("max_pages_per_method",3).asInt();if(maxPages<1||maxPages>100)throw std::runtime_error("Invalid page limit");
 for(int page=0;page<maxPages;++page){bool stop=false;
 Json::Value run;run["domain"]=domain;run["method"]=method;run["offset"]=page*100;
 try {auto data=vk.request(method,domain,page*100);stop=data["items"].size()<100;run["reached_end"]=stop;run["page_limit_reached"]=!stop&&page+1==maxPages;run["status"]="ok";run["posts"]=data["items"].size();
 for(const auto&p:data["items"])for(const auto&a:p["attachments"]){if(a.get("type","").asString()!="doc")continue;auto d=a["doc"];auto ext=normalize(d.get("ext","").asString());if(!supportedDocumentFormat(ext)){++skipped;continue;}
 auto id=std::to_string(d["owner_id"].asInt64())+"_"+std::to_string(d["id"].asInt64());Json::Value b;
 b["id"]=id;b["title"]=d.get("title","");b["title_origin"]="vk_document_title";b["authors"]=Json::arrayValue;b["publication_year"]=Json::nullValue;b["annotation"]=Json::nullValue;b["tags"]=Json::arrayValue;b["format"]=ext;b["size_bytes"]=d.get("size",0);b["source"]="vk";b["community"]=domain;b["post_text"]=p.get("text","");b["source_url"]="https://vk.com/wall"+std::to_string(p["owner_id"].asInt64())+"_"+std::to_string(p["id"].asInt64());
 records[id]=b;if(d["url"].isString())privateUrls[id]=d["url"];
 }
 }catch(const std::exception&e){run["status"]="error";run["message"]=e.what();stop=true;}
 runs.append(run);std::this_thread::sleep_for(std::chrono::seconds(1));if(stop)break;
 }}}
 if(records.empty())throw std::runtime_error("No supported documents collected; existing catalog preserved");
 Json::Value catalog;catalog["schema_version"]=1;catalog["collected_at"]=static_cast<Json::Int64>(std::time(nullptr));catalog["items"]=Json::arrayValue;
 auto path=root/"app/data/catalog.json";if(fs::exists(path)){auto previous=readJson(path);for(const auto&old:previous["items"])if(!records.contains(old["id"].asString()))records[old["id"].asString()]=old;}
 for(const auto&[id,b]:records)catalog["items"].append(b);
 fs::create_directories(root/"app/data/private");fs::permissions(root/"app/data/private",fs::perms::owner_all);
 auto urlsPath=root/"app/data/private/document_urls.json";if(fs::exists(urlsPath)){auto oldUrls=readJson(urlsPath);for(const auto&key:oldUrls.getMemberNames())if(!privateUrls.isMember(key))privateUrls[key]=oldUrls[key];}
 writeJson(urlsPath,privateUrls);fs::permissions(root/"app/data/private/document_urls.json",fs::perms::owner_read|fs::perms::owner_write);
 writeJson(path,catalog);Json::Value report;report["runs"]=runs;report["catalog_documents"]=catalog["items"].size();report["unsupported_attachments_skipped"]=skipped;writeJson(root/"materials/collection-status.json",report);return report;
}
}
