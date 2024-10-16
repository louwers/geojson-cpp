#pragma once

#include <maplibre/geojson.hpp>
#include <maplibre/geojson/rapidjson.hpp>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <sstream>

namespace maplibre {
namespace geojson {

using error    = std::runtime_error;
using prop_map = std::unordered_map<std::string, value>;

void validatePolygon(const rapidjson_value &json) {
    // this check is required incase case of multipolygon validation
    if (!json.IsArray()) {
        throw error("Coordinates must be nested more deeply.");
    }
    for (auto &element : json.GetArray()) {
        if (!element.IsArray()) {
            throw error("Coordinates must be an array of arrays, each describing a polygon.");
        }
        if (element.Size() < 4) {
            throw error("Polygon must be described by 4 or more coordinate points. Improper "
                        "nesting can also lead to this error. Double check that the coordinates "
                        "are properly nested and there are 4 or more coordinates.");
        }
    }
}

void validateLineString(const rapidjson_value &json) {
    if (json.GetArray().Size() < 2) {
        throw error("A line string must have two or more coordinate points.");
    }
}

template <typename T>
T convert(const rapidjson_value &json);

template <>
point convert<point>(const rapidjson_value &json) {
    if (!json.IsArray()) {
        throw error("coordinates must be an array.");
    }
    if (json.Size() < 2)
        throw error("coordinates array must have at least 2 numbers");

    return point{ json[0].GetDouble(), json[1].GetDouble() };
}

template <typename Cont>
Cont convert(const rapidjson_value &json) {
    Cont points;
    if (!json.IsArray()) {
        throw error("coordinates must be an array of points describing linestring or an array of "
                    "arrays describing polygons and line strings.");
    }
    auto size = json.Size();
    points.reserve(size);

    for (auto &element : json.GetArray()) {
        points.push_back(convert<typename Cont::value_type>(element));
    }
    return points;
}

template <>
geometry convert<geometry>(const rapidjson_value &json) {
    if (json.IsNull())
        return empty{};

    if (!json.IsObject())
        throw error("Geometry must be an object");

    const auto &json_end = json.MemberEnd();

    const auto &type_itr = json.FindMember("type");
    if (type_itr == json_end)
        throw error("Geometry must have a type property");

    const auto &type = type_itr->value;

    if (type == "GeometryCollection") {
        const auto &geometries_itr = json.FindMember("geometries");
        if (geometries_itr == json_end)
            throw error("GeometryCollection must have a geometries property");

        const auto &json_geometries = geometries_itr->value;

        if (!json_geometries.IsArray())
            throw error("GeometryCollection geometries property must be an array");

        return geometry{ convert<geometry_collection>(json_geometries) };
    }

    const auto &coords_itr = json.FindMember("coordinates");

    if (coords_itr == json_end)
        throw error(std::string(type.GetString()) + " geometry must have a coordinates property");

    const auto &json_coords = coords_itr->value;
    if (!json_coords.IsArray())
        throw error("coordinates property must be an array");

    if (type == "Point")
        return geometry{ convert<point>(json_coords) };
    if (type == "MultiPoint")
        return geometry{ convert<multi_point>(json_coords) };
    if (type == "LineString") {
        validateLineString(json_coords);
        return geometry{ convert<line_string>(json_coords) };
    }
    if (type == "MultiLineString") {
        for (auto &element : json_coords.GetArray()) {
            validateLineString(element);
        }
        return geometry{ convert<multi_line_string>(json_coords) };
    }
    if (type == "Polygon") {
        validatePolygon(json_coords);
        return geometry{ convert<polygon>(json_coords) };
    }
    if (type == "MultiPolygon") {
        for (auto &element : json_coords.GetArray()) {
            validatePolygon(element);
        }
        return geometry{ convert<multi_polygon>(json_coords) };
    }
    throw error(std::string(type.GetString()) + " not yet implemented");
}

template <>
value convert<value>(const rapidjson_value &json);

template <>
prop_map convert(const rapidjson_value &json) {
    if (!json.IsObject())
        throw error("properties must be an object");

    prop_map result;
    for (auto &member : json.GetObject()) {
        result.emplace(std::string(member.name.GetString(), member.name.GetStringLength()),
                       convert<value>(member.value));
    }
    return result;
}

template <>
value convert<value>(const rapidjson_value &json) {
    switch (json.GetType()) {
    case rapidjson::kNullType:
        return null_value_t{};
    case rapidjson::kFalseType:
        return false;
    case rapidjson::kTrueType:
        return true;
    case rapidjson::kObjectType:
        return convert<prop_map>(json);
    case rapidjson::kArrayType:
        return convert<std::vector<value>>(json);
    case rapidjson::kStringType:
        return std::string(json.GetString(), json.GetStringLength());
    default:
        assert(json.GetType() == rapidjson::kNumberType);
        if (json.IsUint64())
            return std::uint64_t(json.GetUint64());
        if (json.IsInt64())
            return std::int64_t(json.GetInt64());
        return json.GetDouble();
    }
}

template <>
identifier convert<identifier>(const rapidjson_value &json) {
    switch (json.GetType()) {
    case rapidjson::kStringType:
        return std::string(json.GetString(), json.GetStringLength());
    case rapidjson::kNumberType:
        if (json.IsUint64())
            return std::uint64_t(json.GetUint64());
        if (json.IsInt64())
            return std::int64_t(json.GetInt64());
        return json.GetDouble();
    default:
        throw error("Feature id must be a string or number");
    }
}

template <>
feature convert<feature>(const rapidjson_value &json) {
    if (!json.IsObject())
        throw error("Feature must be an object");

    auto const &json_end = json.MemberEnd();
    auto const &type_itr = json.FindMember("type");

    if (type_itr == json_end)
        throw error("Feature must have a type property");
    if (type_itr->value != "Feature")
        throw error("Feature type must be Feature");

    auto const &geom_itr = json.FindMember("geometry");

    if (geom_itr == json_end)
        throw error("Feature must have a geometry property");

    feature result{ convert<geometry>(geom_itr->value) };

    auto const &id_itr = json.FindMember("id");
    if (id_itr != json_end) {
        result.id = convert<identifier>(id_itr->value);
    }

    auto const &prop_itr = json.FindMember("properties");
    if (prop_itr != json_end) {
        const auto &json_props = prop_itr->value;
        if (!json_props.IsNull()) {
            result.properties = convert<prop_map>(json_props);
        }
    }

    return result;
}

template <>
geojson convert<geojson>(const rapidjson_value &json) {
    if (!json.IsObject())
        throw error("GeoJSON must be an object");

    const auto &type_itr = json.FindMember("type");
    const auto &json_end = json.MemberEnd();

    if (type_itr == json_end)
        throw error("GeoJSON must have a type property");

    const auto &type = type_itr->value;

    if (type == "FeatureCollection") {
        const auto &features_itr = json.FindMember("features");
        if (features_itr == json_end)
            throw error("FeatureCollection must have features property");

        const auto &json_features = features_itr->value;

        if (!json_features.IsArray())
            throw error("FeatureCollection features property must be an array");

        feature_collection collection;

        const auto &size = json_features.Size();
        collection.reserve(size);

        for (auto &feature_obj : json_features.GetArray()) {
            collection.push_back(convert<feature>(feature_obj));
        }

        return geojson{ collection };
    }

    if (type == "Feature")
        return geojson{ convert<feature>(json) };

    return geojson{ convert<geometry>(json) };
}

template feature_collection convert<feature_collection>(const rapidjson_value &);

template <class T>
T parse(const std::string &json) {
    rapidjson_document d;
    d.Parse(json.c_str());
    if (d.HasParseError()) {
        std::stringstream message;
        message << d.GetErrorOffset() << " - " << rapidjson::GetParseError_En(d.GetParseError());
        throw error(message.str());
    }
    return convert<T>(d);
}

// Instantiate the template.
template geojson parse<geojson>(const std::string &);
template geometry parse<geometry>(const std::string &);
template feature parse<feature>(const std::string &);
template feature_collection parse<feature_collection>(const std::string &);

// Specialized implementation for geojson.
geojson parse(const std::string &json) {
    return parse<geojson>(json);
}

geojson convert(const rapidjson_value &json) {
    return convert<geojson>(json);
}

template <>
rapidjson_value convert<geometry>(const geometry &, rapidjson_allocator &);

template <>
rapidjson_value convert<feature>(const feature &, rapidjson_allocator &);

template <>
rapidjson_value convert<feature_collection>(const feature_collection &, rapidjson_allocator &);

struct to_type {
public:
    const char *operator()(const empty &) {
        abort();
    }

    const char *operator()(const point &) {
        return "Point";
    }

    const char *operator()(const line_string &) {
        return "LineString";
    }

    const char *operator()(const polygon &) {
        return "Polygon";
    }

    const char *operator()(const multi_point &) {
        return "MultiPoint";
    }

    const char *operator()(const multi_line_string &) {
        return "MultiLineString";
    }

    const char *operator()(const multi_polygon &) {
        return "MultiPolygon";
    }

    const char *operator()(const geometry_collection &) {
        return "GeometryCollection";
    }
};

struct to_coordinates_or_geometries {
    rapidjson_allocator &allocator;

    // Handles line_string, polygon, multi_point, multi_line_string, multi_polygon, and geometry_collection.
    template <class E>
    rapidjson_value operator()(const std::vector<E> &vector) {
        rapidjson_value result;
        result.SetArray();
        for (std::size_t i = 0; i < vector.size(); ++i) {
            result.PushBack(operator()(vector[i]), allocator);
        }
        return result;
    }

    rapidjson_value operator()(const point &element) {
        rapidjson_value result;
        result.SetArray();
        result.PushBack(element.x, allocator);
        result.PushBack(element.y, allocator);
        return result;
    }

    rapidjson_value operator()(const empty &) {
        abort();
    }

    rapidjson_value operator()(const geometry &element) {
        return convert(element, allocator);
    }
};

struct to_value {
    rapidjson_allocator &allocator;

    rapidjson_value operator()(null_value_t) {
        rapidjson_value result;
        result.SetNull();
        return result;
    }

    rapidjson_value operator()(bool t) {
        rapidjson_value result;
        result.SetBool(t);
        return result;
    }

    rapidjson_value operator()(int64_t t) {
        rapidjson_value result;
        result.SetInt64(t);
        return result;
    }

    rapidjson_value operator()(uint64_t t) {
        rapidjson_value result;
        result.SetUint64(t);
        return result;
    }

    rapidjson_value operator()(double t) {
        rapidjson_value result;
        result.SetDouble(t);
        return result;
    }

    rapidjson_value operator()(const std::string &t) {
        rapidjson_value result;
        result.SetString(t.data(), rapidjson::SizeType(t.size()), allocator);
        return result;
    }

    rapidjson_value operator()(const std::vector<value> &array) {
        rapidjson_value result;
        result.SetArray();
        for (const auto &item : array) {
            result.PushBack(std::visit(*this, item), allocator);
        }
        return result;
    }

    rapidjson_value operator()(const std::shared_ptr<std::vector<value>> &array) {
        return this->operator()(*array);
    }

    rapidjson_value operator()(const std::unordered_map<std::string, value> &map) {
        rapidjson_value result;
        result.SetObject();
        for (const auto &property : map) {
            result.AddMember(
                rapidjson::GenericStringRef<char>{ property.first.data(), rapidjson::SizeType(property.first.size()) },
                std::visit(*this, property.second), allocator);
        }
        return result;
    }

    rapidjson_value operator()(const std::shared_ptr<std::unordered_map<std::string, value>> &map) {
        return this->operator()(*map);
    }
};

template <>
rapidjson_value convert<geometry>(const geometry &element, rapidjson_allocator &allocator) {
    if (std::holds_alternative<empty>(element))
        return rapidjson_value(rapidjson::kNullType);

    rapidjson_value result(rapidjson::kObjectType);

    result.AddMember("type", rapidjson::GenericStringRef<char>{ std::visit(to_type(), element) }, allocator);

    result.AddMember(rapidjson::GenericStringRef<char>{ std::holds_alternative<geometry_collection>(element)
                                                            ? "geometries"
                                                            : "coordinates" },
                     std::visit(to_coordinates_or_geometries{ allocator }, element), allocator);

    return result;
}

template <>
rapidjson_value convert<feature>(const feature &element, rapidjson_allocator &allocator) {
    rapidjson_value result(rapidjson::kObjectType);
    result.AddMember("type", "Feature", allocator);

    if (!std::holds_alternative<null_value_t>(element.id)) {
        result.AddMember("id", std::visit(to_value{ allocator }, element.id), allocator);
    }

    result.AddMember("geometry", convert(element.geometry, allocator), allocator);
    result.AddMember("properties", to_value{ allocator }(element.properties), allocator);

    return result;
}

template <>
rapidjson_value convert<feature_collection>(const feature_collection &collection, rapidjson_allocator &allocator) {
    rapidjson_value result(rapidjson::kObjectType);
    result.AddMember("type", "FeatureCollection", allocator);

    rapidjson_value features(rapidjson::kArrayType);
    for (const auto &element : collection) {
        features.PushBack(convert(element, allocator), allocator);
    }
    result.AddMember("features", features, allocator);

    return result;
}

template <>
rapidjson_value convert(const geojson &element, rapidjson_allocator &allocator) {
    return convert(element, allocator);
}

rapidjson_value convert(const geojson &element, rapidjson_allocator &allocator) {
    return std::visit([&](const auto &alternative) { return convert(alternative, allocator); }, element);
}

template <class T>
std::string stringify(const T &t) {
    rapidjson_allocator allocator;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    convert(t, allocator).Accept(writer);
    return buffer.GetString();
}

// Instantiate the template.
template std::string stringify<geometry>(const geometry &);
template std::string stringify<feature>(const feature &);
template std::string stringify<feature_collection>(const feature_collection &);

// Specialized implementation for geojson.
template <>
std::string stringify(const geojson &element) {
    return stringify(element);
}

std::string stringify(const geojson &element) {
    return std::visit([](const auto &alternative) { return stringify(alternative); }, element);
}

} // namespace geojson
} // namespace maplibre
