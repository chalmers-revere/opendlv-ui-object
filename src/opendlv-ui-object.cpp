/*
 * Copyright (C) 2019 Ola Benderius
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <json.hpp>

#include "cluon-complete.hpp"

#include "opendlv-ui-server/http-request.hpp"
#include "opendlv-ui-server/http-response.hpp"
#include "opendlv-ui-server/session-data.hpp"
#include "opendlv-ui-server/opendlv-ui-server.hpp"

int32_t main(int32_t argc, char **argv)
{
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if (0 == commandlineArguments.count("cid") || 0 == commandlineArguments.count("port") || 0 == commandlineArguments.count("http-root")) {
    std::cerr << argv[0] << " is the the graphical interface for the OpenDLV simulation environment." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cid=<libcluon session> --port=<the port where HTTP/WebSocket is served> --http-root=<folder where HTTP content can be found> [--id=<Identifier in case of multiple running instances>] [--verbose]" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cid=111 --port=8000 --http-root=./http" << std::endl;
    retCode = 1;
  } else {
    bool const VERBOSE{commandlineArguments.count("verbose") != 0};
    uint16_t const CID = static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]));

    uint32_t const HTTP_PORT = static_cast<uint32_t>(std::stoi(commandlineArguments["port"]));
    std::string const HTTP_ROOT = commandlineArguments["http-root"];
    
    std::string const SSL_CERT_PATH{(commandlineArguments["ssl-cert-path"].size() != 0) ? commandlineArguments["ssl-cert-path"] : ""};
    std::string const SSL_KEY_PATH{(commandlineArguments["ssl-key-path"].size() != 0) ? commandlineArguments["ssl-key-path"] : ""};
    std::string const MAP_FILENAME{(commandlineArguments["map-file"].size() != 0) ? commandlineArguments["map-file"] : ""};

    auto httpRequestDelegate([&HTTP_ROOT, &MAP_FILENAME, &VERBOSE](HttpRequest const &httpRequest, 
          std::shared_ptr<SessionData>, std::string const & /*clientIp*/) -> std::unique_ptr<HttpResponse>
        {
          if (httpRequest.getPage() == "/map" && !MAP_FILENAME.empty()) {
            nlohmann::json json;
            std::ifstream input(MAP_FILENAME);
            uint32_t i{0};

            for (std::string str; getline(input, str); i++) {
              std::cout << "Parsed line: " << str << std::endl;
              if (i == 0) {
                continue;
              }
              std::vector<std::string> objectParams = stringtoolbox::split(
                  stringtoolbox::trim(str), ';');
              std::cout << "Num: " << objectParams.size() << std::endl;
              if (objectParams.size() == 4) {
                int32_t id{std::stoi(objectParams[0])};
                int32_t type{std::stoi(objectParams[1])};
                double lat{std::stof(objectParams[2])};
                double lon{std::stof(objectParams[3])};

                nlohmann::json inner1;
                inner1["id"] = id;
                inner1["type"] = type;
                inner1["latitude"] = lat;
                inner1["longitude"] = lon;

                if (VERBOSE) {
                  std::cout << "Found object id=" << id << " type=" << type 
                    << " at " << lat << ", " << lon << std::endl;
                }

                json["object"].push_back(inner1);
              }
            }

            std::unique_ptr<HttpResponse> response(
                new HttpResponse("text/plain", json.dump()));
            return response;
          } else {
            std::string const PAGE = (httpRequest.getPage() != "/") ? httpRequest.getPage() : std::string("/index.html");
            std::experimental::filesystem::path path{HTTP_ROOT + PAGE};

            if (!std::experimental::filesystem::exists(path)) {
              std::cout << "ERROR: file '" << path.string() <<  "' not found." << std::endl;
              return nullptr;
            }
            
            std::ifstream ifs(path.string());
            std::stringstream ss;
            ss << ifs.rdbuf();
            std::string content = ss.str();

            std::string contentType;
            std::string const EXTENSION = path.extension();
            if (EXTENSION == ".html") {
              contentType = "text/html";
            } else if (EXTENSION == ".css") {
              contentType = "text/css";
            } else if (EXTENSION == ".js") {
              contentType = "text/javascript";
            } else if (EXTENSION == ".json") {
              contentType = "application/json";
            } else if (EXTENSION == ".gif") {
              contentType = "image/gif";
            } else if (EXTENSION == ".png") {
              contentType = "image/png";
            } else if (EXTENSION == ".jpeg" || EXTENSION == ".jpg") {
              contentType = "image/jpeg";
            } else {
              contentType = "text/plain";
            }

            std::unique_ptr<HttpResponse> response(new HttpResponse(contentType, content));
            return response;
          }
        });
    WebsocketServer ws(HTTP_PORT, httpRequestDelegate, nullptr, SSL_CERT_PATH, SSL_KEY_PATH);

    auto onIncomingEnvelope([&ws, &VERBOSE](cluon::data::Envelope &&envelope) {
        std::string data = cluon::serializeEnvelope(std::move(envelope));
        ws.sendDataToAllClients(data);
        if (VERBOSE) {
          std::cout << "Sending message " << envelope.dataType() << " (" << data.size() << " bytes) to all websocket clients." << std::endl;
        }
      });
    cluon::OD4Session od4{CID, onIncomingEnvelope};

    auto dataReceivedDelegate([&od4, &VERBOSE](std::string const &message, std::string const &clientIp, uint32_t httpClientId) {
        std::stringstream sstr(message);
        while (sstr.good()) {
          auto tmp{cluon::extractEnvelope(sstr)};
          if (tmp.first) {
            cluon::data::Envelope env{tmp.second};
            env.sent(cluon::time::now());
            env.sampleTimeStamp(cluon::time::now());
            od4.send(std::move(env));
            if (VERBOSE) {
              std::cout << "Got message (" << env.dataType() << ") from client " << httpClientId << " (" << clientIp << ")." << std::endl; 
            }
          }
        }
      });
    ws.setDataReceiveDelegate(dataReceivedDelegate);

    while (od4.isRunning()) {
      ws.stepServer();
    }
  }
  return retCode;
}
