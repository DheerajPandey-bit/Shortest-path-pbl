#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <cctype>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;
using json = nlohmann::json;

struct Node
{
    long long id;
    double lat, lon;
    string name;
};
struct Edge
{
    int to;
    double km, speedKmh;
};
struct Route
{
    vector<int> path;
    double km = 0, minutes = 0, score = numeric_limits<double>::infinity();
};
struct RoadGraph
{
    vector<Node> nodes;
    vector<vector<Edge>> adj;
};

static constexpr double EARTH_KM = 6371.0088;
double haversine(double lat1, double lon1, double lat2, double lon2) {

    const double PI = 3.14159265358979323846;

    auto rad = [PI](double x) -> double {
        return x * PI / 180.0;
    };

    double dLat = rad(lat2 - lat1);
    double dLon = rad(lon2 - lon1);

    double a =
        sin(dLat / 2) * sin(dLat / 2) +
        cos(rad(lat1)) * cos(rad(lat2)) *
        sin(dLon / 2) * sin(dLon / 2);

    double c = 2 * atan2(
        sqrt(a),
        sqrt(max(0.0, 1.0 - a))
    );

    return EARTH_KM * c;
}
string urlEncode(const string &s)
{
    ostringstream out;
    out << hex;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else if (c == ' ')
            out << "%20";
        else
            out << '%' << uppercase << setw(2) << setfill('0') << (int)c << nouppercase;
    }
    return out.str();
}

double speedFor(const string &highway)
{
    if (highway == "motorway" || highway == "trunk")
        return 80;
    if (highway == "primary")
        return 55;
    if (highway == "secondary")
        return 40;
    if (highway == "tertiary")
        return 30;
    if (highway == "residential" || highway == "unclassified")
        return 25;
    if (highway == "service")
        return 15;
    return 20;
}

bool loadRoadGraph(double sLat, double sLon, double dLat, double dLon, RoadGraph &g, string &error)
{
    double minLat = min(sLat, dLat) - 0.025, maxLat = max(sLat, dLat) + 0.025;
    double minLon = min(sLon, dLon) - 0.025, maxLon = max(sLon, dLon) + 0.025;
    if (maxLat - minLat > 0.30 || maxLon - minLon > 0.30)
    {
        error = "The locations are too far apart for this prototype's road-data download. Try places within roughly 30 km.";
        return false;
    }
    ostringstream bbox;
    bbox << minLat << "," << minLon << "," << maxLat << "," << maxLon;
    string query = "[out:json][timeout:25];(way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|service|living_street)$\"](" + bbox.str() + "););out body;>;out skel qt;";

    httplib::SSLClient cli("overpass-api.de", 443);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(35, 0);
    cli.set_follow_location(true);
    auto response = cli.Get("/api/interpreter?data=" + urlEncode(query));
    if (!response || response->status != 200)
    {
        error = "Could not download OpenStreetMap road data. Check your internet connection and try again.";
        return false;
    }

    json data;
    try
    {
        data = json::parse(response->body);
    }
    catch (...)
    {
        error = "The road-data service returned an unreadable response. Please retry.";
        return false;
    }
    if (!data.contains("elements") || !data["elements"].is_array())
    {
        error = "No road data was returned for this area.";
        return false;
    }

    unordered_map<long long, int> index;
    for (const auto &el : data["elements"])
    {
        if (el.value("type", string()) != "node")
            continue;
        Node n;
        n.id = el.value("id", 0LL);
        n.lat = el.value("lat", 0.0);
        n.lon = el.value("lon", 0.0);
        n.name = to_string(n.id);
        index[n.id] = (int)g.nodes.size();
        g.nodes.push_back(n);
    }
    g.adj.resize(g.nodes.size());
    auto add = [&](int u, int v, double speed)
    {
        double km = haversine(g.nodes[u].lat, g.nodes[u].lon, g.nodes[v].lat, g.nodes[v].lon);
        if (km > 0.0001)
            g.adj[u].push_back({v, km, speed});
    };
    int roadCount = 0;
    for (const auto &el : data["elements"])
    {
        if (el.value("type", string()) != "way" || !el.contains("nodes") || !el["nodes"].is_array())
            continue;
        string highway = "residential", oneway = "no";
        double speed = 25;
        if (el.contains("tags") && el["tags"].is_object())
        {
            auto tags = el["tags"];
            highway = tags.value("highway", highway);
            speed = speedFor(highway);
            oneway = tags.value("oneway", oneway);
            if (tags.contains("maxspeed"))
            {
                string ms = tags["maxspeed"].is_string() ? tags["maxspeed"].get<string>() : "";
                try
                {
                    double parsed = stod(ms);
                    if (parsed > 0 && parsed < 160)
                        speed = parsed;
                }
                catch (...)
                {
                }
            }
        }
        vector<int> ids;
        for (const auto &id : el["nodes"])
        {
            long long key = id.get<long long>();
            if (index.count(key))
                ids.push_back(index[key]);
            else
                ids.push_back(-1);
        }
        for (size_t i = 1; i < ids.size(); ++i)
        {
            int a = ids[i - 1], b = ids[i];
            if (a < 0 || b < 0)
                continue;
            if (oneway == "-1")
                add(b, a, speed);
            else
            {
                add(a, b, speed);
                if (oneway != "yes" && oneway != "1" && oneway != "true")
                    add(b, a, speed);
            }
            ++roadCount;
        }
    }
    if (g.nodes.empty() || roadCount == 0)
    {
        error = "No usable roads found near these locations. Add the city or state to each place name.";
        return false;
    }
    return true;
}

int nearestNode(const RoadGraph &g, double lat, double lon)
{
    int best = -1;
    double bestDist = numeric_limits<double>::infinity();
    for (int i = 0; i < (int)g.nodes.size(); ++i)
    {
        double d = haversine(lat, lon, g.nodes[i].lat, g.nodes[i].lon);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

Route dijkstra(const RoadGraph &g, int src, int dst, const string &mode, double traffic, int banU = -1, int banV = -1)
{
    const double INF = numeric_limits<double>::infinity();
    vector<double> dist(g.nodes.size(), INF);
    vector<int> parent(g.nodes.size(), -1);
    using State = pair<double, int>;
    priority_queue<State, vector<State>, greater<State>> pq;
    dist[src] = 0;
    pq.push({0, src});
    while (!pq.empty())
    {
        auto [cost, u] = pq.top();
        pq.pop();
        if (cost > dist[u])
            continue;
        if (u == dst)
            break;
        for (const auto &e : g.adj[u])
        {
            if (u == banU && e.to == banV)
                continue;
            double mins = e.km / e.speedKmh * 60.0 * traffic;
            double w = (mode == "time") ? mins : e.km;
            double nd = cost + w;
            if (nd < dist[e.to])
            {
                dist[e.to] = nd;
                parent[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }
    Route r;
    if (!isfinite(dist[dst]))
        return r;
    for (int at = dst; at != -1; at = parent[at])
        r.path.push_back(at);
    reverse(r.path.begin(), r.path.end());
    r.score = dist[dst];
    for (size_t i = 1; i < r.path.size(); ++i)
    {
        int u = r.path[i - 1], v = r.path[i];
        for (const auto &e : g.adj[u])
            if (e.to == v)
            {
                r.km += e.km;
                r.minutes += e.km / e.speedKmh * 60.0 * traffic;
                break;
            }
    }
    return r;
}

string routeJson(const RoadGraph &g, const Route &r, double startLat, double startLon, double endLat, double endLon)
{
    if (r.path.empty())
        return "null";
    json points = json::array();
    points.push_back({{"name", "Starting location"}, {"lat", startLat}, {"lng", startLon}});
    for (int i : r.path)
        points.push_back({{"name", g.nodes[i].name}, {"lat", g.nodes[i].lat}, {"lng", g.nodes[i].lon}});
    points.push_back({{"name", "Destination"}, {"lat", endLat}, {"lng", endLon}});
    return json({{"distanceKm", r.km + haversine(startLat, startLon, g.nodes[r.path.front()].lat, g.nodes[r.path.front()].lon) + haversine(endLat, endLon, g.nodes[r.path.back()].lat, g.nodes[r.path.back()].lon)},
                 {"minutes", r.minutes},
                 {"nodes", points}})
        .dump();
}

int main()
{
    httplib::Server server;
    server.Get("/api/route", [](const httplib::Request &req, httplib::Response &res)
               {
        auto number=[&](const char* key,double& value){try{value=stod(req.get_param_value(key));return isfinite(value);}catch(...){return false;}};
        double sLat,sLon,dLat,dLon;
        if(!number("sourceLat",sLat)||!number("sourceLng",sLon)||!number("destinationLat",dLat)||!number("destinationLng",dLon)) {
            res.status=400;res.set_content(R"({"error":"Missing or invalid location coordinates."})","application/json");return;
        }
        string mode=req.get_param_value("mode"),traffic=req.get_param_value("traffic"); if(mode!="time")mode="distance";
        double factor=traffic=="high"?1.75:(traffic=="medium"?1.30:1.0);
        RoadGraph graph; string error;
        if(!loadRoadGraph(sLat,sLon,dLat,dLon,graph,error)) {res.status=502;res.set_content(json({{"error",error}}).dump(),"application/json");return;}
        int src=nearestNode(graph,sLat,sLon),dst=nearestNode(graph,dLat,dLon);
        if(src<0||dst<0||src==dst){res.status=400;res.set_content(R"({"error":"Could not connect both places to different road nodes. Try more precise addresses."})","application/json");return;}
        Route best=dijkstra(graph,src,dst,mode,factor);
        if(best.path.empty()){res.status=404;res.set_content(R"({"error":"No road route found between these locations in the downloaded map area."})","application/json");return;}
        Route alt;
        for(size_t i=1;i<best.path.size();++i){Route candidate=dijkstra(graph,src,dst,mode,factor,best.path[i-1],best.path[i]);if(!candidate.path.empty()&&candidate.path!=best.path&&candidate.score<alt.score)alt=candidate;}
        json output={{"mode",mode},{"traffic",traffic},{"best",json::parse(routeJson(graph,best,sLat,sLon,dLat,dLon))},
                     {"alternative",alt.path.empty()?json(nullptr):json::parse(routeJson(graph,alt,sLat,sLon,dLat,dLon))}};
        res.set_content(output.dump(),"application/json"); });
    server.set_mount_point("/", "./frontend");
    cout << "Shortest Path Optimizer running at http://localhost:8090\n";
    cout << "Road graph is downloaded from OpenStreetMap when a route is requested.\n";
    server.listen("0.0.0.0", 8090);
}
