#include <fastcgi2/component.h>
#include <fastcgi2/component_factory.h>
#include <fastcgi2/handler.h>
#include <fastcgi2/request.h>

#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <set>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>


using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;


class HelloFastCGI : virtual public fastcgi::Component, virtual public fastcgi::Handler
{
    public:
        mongocxx::client connection;
        std::map<std::string, int> likes;
        std::thread likesThread;
        bool isExit = false;
        HelloFastCGI(fastcgi::ComponentContext *context) :
                fastcgi::Component(context)
        {
            mongocxx::instance inst{};
            connection = mongocxx::client{mongocxx::uri{}};
        }

        void likeUpdater(){ 
            auto collection = connection["vitux"]["anecdote"];
            while (!isExit) {
                std::map<std::string, int> mp;
                std::swap(mp, likes);
                for (auto it: mp) {
                    bsoncxx::oid oid = bsoncxx::oid(it.first);
                    auto doc = collection.update_one(document{} << "_id" << oid << finalize,
                        document{} << "$inc" << open_document << 
                        "rating" << it.second << close_document << finalize);
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }

        virtual void onLoad()
        {
            likesThread = std::thread(&HelloFastCGI::likeUpdater, this);
        }

        virtual void onUnload()
        {
            isExit = true;
            likesThread.join();
        }

        std::vector<std::string> splitPath(std::string & path) {
            std::vector<std::string> splitted;
            std::string current = "";
            for (int i = 1; i < path.length(); ++i) {
                if (path[i] == '/') {
                    splitted.push_back(current);
                    current = "";
                } else {
                    current += path[i];
                }
            }
            if (current != "") {
                splitted.push_back(current);
            }
            return splitted;
        }

        void processMain(std::string & result, std::string & error, int & statusCode) {
            result = "{\"top\": \"/top?page=1\", \"add\": \"/add\"}";
        }

        void processAdd(fastcgi::Request *request, std::vector<std::string> & splittedPath, std::string & result, std::string & error, int & statusCode) {
            if (request->hasArg("text")) {
                auto collection = connection["vitux"]["anecdote"];
                document document{};
                bsoncxx::types::b_date created = bsoncxx::types::b_date(std::chrono::system_clock::now());
                document << "text" << request->getArg("text") 
                         << "created" << created
                         << "rating" << 0;
                bsoncxx::stdx::optional<mongocxx::result::insert_one> retVal = collection.insert_one(document.view());
                if (retVal) {
                    bsoncxx::oid oid = retVal->inserted_id().get_oid().value;
                    std::string objID = oid.to_string();
                    statusCode = 201;
                    std::stringstream screated;
                    screated << created.to_int64();
                    std::string created_str = screated.str();
                    result = "{\"created\": " + created_str + ", \"vote\": \"/upvote/" + objID + 
                    ", \"rating\": 0, \"text\": \"" + request->getArg("text") + "\"}";
                    request->setHeader("Location", "/anecdote/" + objID);
                } else {
                    statusCode = 500;
                }
            } else {
                statusCode = 400;
            }
        }

        void processUpvote(std::vector<std::string> & splittedPath, std::string & result, std::string & error, int & statusCode) {
            // implementation differs from the plan
            // nothing is returned to make request lightweight
            auto collection = connection["vitux"]["anecdote"];
            ++likes[splittedPath[1]];
            statusCode = 200;
        }

        void processTop(fastcgi::Request *request, std::vector<std::string> & splittedPath, std::string & result, std::string & error, int & statusCode) {
            if (request->hasArg("page")) {

                std::string page = "0" + request->getArg("page");
                size_t sz;
                int ind = stoi(page, &sz);
                if (ind == 0 || page.length() != sz) {
                    statusCode = 400;
                } else {
                    auto collection = connection["vitux"]["anecdote"];
                    int count = collection.count({});
                    int last_page = (count + 9) / 10;
                    mongocxx::options::find opts{};
                    auto order = document{} << "rating" << -1 << finalize;
                    opts.sort(order.view());
                    opts.skip((ind - 1) * 10);
                    opts.limit(10);
                    auto docs = collection.find({}, opts);


                    std::stringstream build_resp;
                    build_resp << "{";
                    if (ind + 1 <= last_page) {
                        build_resp << "\"next\": \"/top?page=" << ind + 1 << "\",\n";
                    }
                    if (1 <= ind - 1 && ind - 1 <= last_page) {
                        build_resp << "\"prev\": \"/top?page=" << ind - 1 << "\",\n";
                    }
                    build_resp << "\"anecdote_list\":[";

                    bool isFirst = true;
                    std::set<std::string> was; 
                    for (auto&& doc: docs) {
                        std::string oid = doc["_id"].get_oid().value.to_string();
                        if (was.find(oid) != was.end()) {
                            continue;
                        }
                        was.insert(oid);
                        if (!isFirst) {
                            build_resp << ",\n";
                        }
                        isFirst = false;
                        build_resp << "\"/anecdote/" + oid + "\"";
                    }
                    build_resp << "\n]\n}";
                    result = build_resp.str();
                }
            } else {
                statusCode = 400;
            }
        }

        void processAnecdote(std::vector<std::string> & splittedPath, std::string & result, std::string & error, int & statusCode) {
            auto collection = connection["vitux"]["anecdote"];
            bsoncxx::oid oid = bsoncxx::oid(splittedPath[1]);
            bsoncxx::stdx::optional<bsoncxx::document::value> doc = 
            collection.find_one(document{} << "_id" << oid << finalize);
            if (doc) {
                auto dv = doc->view();
                std::string text = dv["text"].get_utf8().value.to_string();
                int rating = dv["rating"].get_int32().value;
                long long created = dv["created"].get_date().value.count() / 1000;
                std::string oid = dv["_id"].get_oid().value.to_string();
                std::stringstream build_resp;
                build_resp << "{\n\"created\": " << created << ",\n";
                build_resp << "\"vote\": \"/upvote/" + oid + "\",\n";
                build_resp << "\"rating\": " << rating << ",\n";
                build_resp << "\"text\": \"" << text << "\"}";
                result = build_resp.str();
            } else {
                statusCode = 404;
            }
        }

        void setWrongMethodError(std::string & error, int & statusCode) {
            error = "unexpected method";
            statusCode = 400;
        }

        void processRequest(
            fastcgi::Request *request,
            std::vector<std::string> & splittedPath, 
            std::string & method, 
            std::string & result, 
            std::string & error, 
            int & statusCode) {
            if (splittedPath.size() == 0) {
                if (method == "GET") {
                    processMain(result, error, statusCode);
                } else {
                    setWrongMethodError(error, statusCode);
                }
                return;
            } 
            if (splittedPath[0] == "add") {
                if (method == "POST") {
                    processAdd(request, splittedPath, result, error, statusCode);
                } else {
                    setWrongMethodError(error, statusCode);
                }
                return;
            }
            if (splittedPath[0] == "upvote") {
                if (method == "POST") {
                    processUpvote(splittedPath, result, error, statusCode);
                } else {
                    setWrongMethodError(error, statusCode);
                }
                return;
            }
            if (splittedPath[0] == "top") {
                if (method == "GET") {
                    processTop(request, splittedPath, result, error, statusCode);
                } else {
                    setWrongMethodError(error, statusCode);
                }
                return;
            }
            if (splittedPath[0] == "anecdote") {
                if (method == "GET") {
                    processAnecdote(splittedPath, result, error, statusCode);
                } else {
                    setWrongMethodError(error, statusCode);
                }
                return;
            }

            error = "unexpected path";
            statusCode = 502;
        }

        virtual void handleRequest(fastcgi::Request *request, fastcgi::HandlerContext *context)
        {
            std::string scriptName = request->getScriptName();
            std::string query = request->getQueryString();

            std::vector<std::string> splittedPath = splitPath(scriptName);
            std::string result;
            std::string error; // TODO: remove it
            int statusCode = 200;
            std::string method = request->getRequestMethod();
            processRequest(request, splittedPath, method, result, error, statusCode);
            request->setContentType("text/plain");
            
            if (statusCode != 200 && statusCode != 201) {
                request->sendError(statusCode);
            } else {
                request->setStatus(statusCode);
                request->setHeader("Content-Type", "application/json");
                std::stringbuf buffer(result);
                request->write(&buffer);
            }
        }
};

FCGIDAEMON_REGISTER_FACTORIES_BEGIN()
FCGIDAEMON_ADD_DEFAULT_FACTORY("HelloFastCGIFactory", HelloFastCGI)
FCGIDAEMON_REGISTER_FACTORIES_END()
