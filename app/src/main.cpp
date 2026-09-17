#include "core.hpp"
#include <drogon/drogon.h>
#include <curl/curl.h>
#include <iostream>
#include <charconv>
#include <fstream>
#include <sstream>
int main(int argc,char**argv){
 try{
 if(argc<3){std::cerr<<"Usage: vk_books <sync|search|serve> <project-root> [query]\n";return 2;}
 auto root=std::filesystem::absolute(argv[2]);std::string command=argv[1];
 if(command=="sync"){if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)return 1;try{std::cout<<books::sync(root).toStyledString();}catch(...){curl_global_cleanup();throw;}curl_global_cleanup();return 0;}
 auto catalog=std::make_shared<books::JsonCatalog>(root/"app/data/catalog.json");auto search=std::make_shared<books::SearchService>(*catalog,root/"app/data/text");
 if(command=="search"){books::Query q;if(argc>3)q.text=argv[3];std::cout<<search->search(q).toStyledString();return 0;}
 if(command!="serve")return 2;
 drogon::app().setDocumentRoot((root/"app/web").string());
 // Serve only these public files: never expose the project root or cached credentials.
 for(const auto &[route,file]:std::vector<std::pair<std::string,std::string>>{{"/","index.html"},{"/style.css","style.css"},{"/app.js","app.js"}}){
  std::ifstream input(root/"app/web"/file);if(!input)throw std::runtime_error("Web asset missing: "+file);
  std::ostringstream buffer;buffer<<input.rdbuf();auto content=buffer.str();
  drogon::app().registerHandler(route,[content,file](const drogon::HttpRequestPtr&,std::function<void(const drogon::HttpResponsePtr&)>&&cb){
   auto response=drogon::HttpResponse::newHttpResponse();response->setBody(content);
   response->setContentTypeCode(file=="index.html"?drogon::CT_TEXT_HTML:file=="style.css"?drogon::CT_TEXT_CSS:drogon::CT_TEXT_JAVASCRIPT);
   response->addHeader("Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
   response->addHeader("X-Content-Type-Options","nosniff");cb(response);
  },{drogon::Get});
 }
 drogon::app().registerHandler("/api/v1/communities",[root](const drogon::HttpRequestPtr&,std::function<void(const drogon::HttpResponsePtr&)>&&cb){
  try{auto config=books::readJson(root/"config/vk-sources.example.json");Json::Value out;out["items"]=config["communities"];cb(drogon::HttpResponse::newHttpJsonResponse(out));}
  catch(...){auto response=drogon::HttpResponse::newHttpResponse();response->setStatusCode(drogon::k503ServiceUnavailable);cb(response);}
 },{drogon::Get});
 drogon::app().registerHandler("/health",[](const drogon::HttpRequestPtr&,std::function<void(const drogon::HttpResponsePtr&)>&&cb){Json::Value b;b["status"]="ok";b["service"]="vk-book-search";cb(drogon::HttpResponse::newHttpJsonResponse(b));},{drogon::Get});
 drogon::app().registerHandler("/api/v1/books",[catalog,search](const drogon::HttpRequestPtr&r,std::function<void(const drogon::HttpResponsePtr&)>&&cb){
  try{books::Query q;q.text=r->getParameter("q");q.title=r->getParameter("title");q.author=r->getParameter("author");q.tag=r->getParameter("tag");q.community=r->getParameter("community");
  if(!r->getParameter("scope").empty())q.scope=r->getParameter("scope");if(!r->getParameter("title_match").empty())q.titleMatch=r->getParameter("title_match");
  auto integer=[&](const char*name,int fallback){auto s=r->getParameter(name);if(s.empty())return fallback;int v=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);if(e!=std::errc{}||p!=s.data()+s.size())throw std::invalid_argument("Invalid numeric parameter");return v;};q.year=integer("year",0);q.limit=integer("limit",20);q.offset=integer("offset",0);
  cb(drogon::HttpResponse::newHttpJsonResponse(search->search(q)));
  }catch(const std::invalid_argument&e){Json::Value b;b["error"]=e.what();auto resp=drogon::HttpResponse::newHttpJsonResponse(b);resp->setStatusCode(drogon::k400BadRequest);cb(resp);}catch(...){Json::Value b;b["error"]="Catalog or index unavailable";auto resp=drogon::HttpResponse::newHttpJsonResponse(b);resp->setStatusCode(drogon::k503ServiceUnavailable);cb(resp);}
 },{drogon::Get});
 std::cout<<"API: http://127.0.0.1:8081/api/v1/books\n";
 drogon::app().addListener("127.0.0.1",8081).setThreadNum(2).run();
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
