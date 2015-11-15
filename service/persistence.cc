//
// Created by wastl on 15.11.15.
//

#include "persistence.h"
#include "leveldb/cache.h"
#include "leveldb/write_batch.h"

using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using google::protobuf::Int64Value;
using google::protobuf::Message;

namespace marmotta {
namespace persistence {

leveldb::DB* buildDB(const std::string& path, const std::string& suffix) {
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, path + "_" + suffix + ".db", &db);
    assert(status.ok());
    return db;
}

void encodeInt(char* buffer, size_t data) {
    buffer[0] = (char)((data >> 56) & 0xFF);
    buffer[1] = (char)((data >> 48) & 0xFF);
    buffer[2] = (char)((data >> 40) & 0xFF);
    buffer[3] = (char)((data >> 32) & 0xFF);
    buffer[4] = (char)((data >> 24) & 0xFF);
    buffer[5] = (char)((data >> 16) & 0xFF);
    buffer[6] = (char)((data >> 8) & 0xFF);
    buffer[7] = (char)(data & 0xFF);
}

// Creates a cache key based on hashing values of the 4 messages in proper order.
char* LevelDBService::cacheKey(const std::string* a, const std::string* b, const std::string* c, const std::string* d) {
    char* result = (char*)calloc(32, sizeof(char));

    int offset = 0;
    for (auto m : {a, b, c, d}) {
        if (m != nullptr) {
            size_t h = hash_fn(*m);
            encodeInt(&result[offset], h);
        } else {
            return result;
        }
        offset += 8;
    }
    return result;
}

LevelDBService::LevelDBService(const std::string &path)
    : db_spoc(buildDB(path, "spoc")), db_cspo(buildDB(path, "cspo"))
    , db_opsc(buildDB(path, "opsc")), db_cops(buildDB(path, "cops"))
    , db_ns_prefix(buildDB(path, "ns_prefix")), db_ns_url(buildDB(path, "ns_url")) { }

Status LevelDBService::AddNamespaces(
        ServerContext* context, ServerReader<rdf::proto::Namespace>* reader, Int64Value* result) {

    int64_t count = 0;

    rdf::proto::Namespace ns;

    leveldb::WriteBatch batch_prefix, batch_url;
    std::string buffer;
    while (reader->Read(&ns)) {
        ns.SerializeToString(&buffer);
        batch_prefix.Put(ns.prefix(), buffer);
        batch_url.Put(ns.uri(), buffer);
        count++;
    }
    db_ns_prefix->Write(leveldb::WriteOptions(), &batch_prefix);
    db_ns_url->Write(leveldb::WriteOptions(), &batch_url);

    result->set_value(count);

    return Status::OK;
}

Status LevelDBService::AddStatements(
        ServerContext* context, ServerReader<rdf::proto::Statement>* reader, Int64Value* result) {

    int64_t count = 0;

    rdf::proto::Statement stmt;

    leveldb::WriteBatch batch_spoc, batch_cspo, batch_opsc, batch_cops;
    std::string buffer, bufs, bufp, bufo, bufc;
    while (reader->Read(&stmt)) {
        stmt.SerializeToString(&buffer);

        stmt.subject().SerializeToString(&bufs);
        stmt.predicate().SerializeToString(&bufp);
        stmt.object().SerializeToString(&bufo);
        stmt.context().SerializeToString(&bufc);

        char* k_spoc = cacheKey(&bufs, &bufp, &bufo, &bufc);
        batch_spoc.Put(leveldb::Slice(k_spoc, 32), buffer);
        free(k_spoc);

        char* k_cspo = cacheKey(&bufc, &bufs, &bufp, &bufo);
        batch_cspo.Put(leveldb::Slice(k_cspo, 32), buffer);
        free(k_cspo);

        char* k_opsc = cacheKey(&bufo, &bufp, &bufs, &bufc);
        batch_opsc.Put(leveldb::Slice(k_opsc, 32), buffer);
        free(k_opsc);

        char* k_cops = cacheKey(&bufc, &bufo, &bufp, &bufs);
        batch_cops.Put(leveldb::Slice(k_cops, 32), buffer);
        free(k_cops);

        count++;
    }
    db_cops->Write(leveldb::WriteOptions(), &batch_cops);
    db_opsc->Write(leveldb::WriteOptions(), &batch_opsc);
    db_cspo->Write(leveldb::WriteOptions(), &batch_cspo);
    db_spoc->Write(leveldb::WriteOptions(), &batch_spoc);

    result->set_value(count);

    return Status::OK;
}


}  // namespace persistence
}  // namespace marmotta


int main(int argc, const char** argv) {
    std::string server_address("0.0.0.0:10000");
    if (argc > 1) {
        server_address = argv[1];
    }

    marmotta::persistence::LevelDBService service("/tmp/testdb");

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Persistence Server listening on " << server_address << std::endl;

    server->Wait();

    return 0;
}