//
// Created by wastl on 18.04.15.
//
#include <raptor2/raptor2.h>
#include "rdf_serializer.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>

namespace marmotta {
    static std::map<std::string, rdf::URI> namespacesMap(std::vector<rdf::Namespace> list) {
        std::map<std::string, rdf::URI> result;
        for(auto it=list.cbegin(); it != list.cend(); it++) {
            result[it->getPrefix()]=it->getUri();
        }
        return result;
    }

    static int std_iostream_write_byte(void *context, const int byte) {
        std::ostream* out = (std::ostream *) context;
        out->write((char const *) &byte, 1);
        if(*out) {
            return 0;
        } else {
            return 1;
        }
    }

    static int std_iostream_write_bytes(void *context, const void *ptr, size_t size, size_t nmemb) {
        std::ostream* out = (std::ostream *) context;
        out->write((char const *) ptr, size * nmemb);
        if(*out) {
            return 0;
        } else {
            return 1;
        }
    }

    static int std_iostream_read_bytes(void *context, void *ptr, size_t size, size_t nmemb) {
        std::istream* in = (std::istream *)context;

        if(!*in) {
            return -1;
        }

        in->read((char *) ptr, size*nmemb);
        return (int) in->gcount();
    }

    static int std_iostream_read_eof(void *context) {
        std::istream* in = (std::istream *)context;

        if(in->eof()) {
            return 1;
        } else {
            return 0;
        }
    }

    const raptor_iostream_handler raptor_handler = {
            2, NULL, NULL,
            &std_iostream_write_byte, &std_iostream_write_bytes, NULL,
            &std_iostream_read_bytes, &std_iostream_read_eof
    };


    namespace serializer {
        Serializer::Serializer(const rdf::URI &baseUri, Format format, std::vector<rdf::Namespace> namespaces) {
            switch(format) {
                case PROTO:
                case PROTO_TEXT:
                    impl.reset(new ProtoSerializer(baseUri, format, namespaces));
                    break;
                default:
                    impl.reset(new RaptorSerializer(baseUri, format, namespaces));
            }
        }

        Serializer::Serializer(const rdf::URI &baseUri, Format format, std::map<std::string, rdf::URI> namespaces) {
            switch(format) {
                case PROTO:
                case PROTO_TEXT:
                    impl.reset(new ProtoSerializer(baseUri, format, namespaces));
                    break;
                default:
                    impl.reset(new RaptorSerializer(baseUri, format, namespaces));
            }
        }


        SerializerBase::SerializerBase(const rdf::URI& baseUri, Format format, std::vector<rdf::Namespace> namespaces)
                : baseUri(baseUri), format(format), namespaces(namespacesMap(namespaces)) { }

        SerializerBase::SerializerBase(const rdf::URI& baseUri, Format format, std::map<std::string, rdf::URI> namespaces)
                : baseUri(baseUri), format(format), namespaces(namespaces) { }


        inline std::string raptorFormat(Format format) {
            switch (format) {
                case Format::RDFXML:
                    return "rdfxml";
                case Format::RDFXML_ABBREV:
                    return "rdfxml-abbrev";
                case Format::GRAPHVIZ:
                    return "dot";
                case Format::NQUADS:
                    return "nquads";
                case Format::NTRIPLES:
                    return "ntriples";
                case Format::TURTLE:
                    return "turtle";
                case Format::RDFJSON:
                    return "json";
                case Format::SPARQL_JSON:
                    return "json-triples";
                default:
                    return "rdfxml";
            }
        }


        RaptorSerializer::RaptorSerializer(const rdf::URI& baseUri, Format format)
                : SerializerBase(baseUri, format) {

            world = raptor_new_world();
            base  = raptor_new_uri(world, (unsigned char const *) baseUri.getUri().c_str());
            initRaptor();
        }

        RaptorSerializer::RaptorSerializer(const rdf::URI& baseUri, Format format, std::vector<rdf::Namespace> namespaces)
                : SerializerBase(baseUri, format, namespaces) {

            world = raptor_new_world();
            base  = raptor_new_uri(world, (unsigned char const *) baseUri.getUri().c_str());
            initRaptor();
        }

        RaptorSerializer::RaptorSerializer(const rdf::URI& baseUri, Format format, std::map<std::string, rdf::URI> namespaces)
                : SerializerBase(baseUri, format, namespaces) {

            world = raptor_new_world();
            base  = raptor_new_uri(world, (unsigned char const *) baseUri.getUri().c_str());
            initRaptor();
        }


        RaptorSerializer::~RaptorSerializer() {
            // check for NULL in case a move operation has set the fields to a null pointer
            if(serializer != NULL)
                raptor_free_serializer(serializer);

            if(base != NULL)
                raptor_free_uri(base);

            if(world != NULL)
                raptor_free_world(world);

        }

        /*
        RaptorSerializer::RaptorSerializer(const RaptorSerializer &other) {
            format = other.format;
            namespaces = other.namespaces;

            world = raptor_new_world();
            base  = raptor_new_uri(world, raptor_uri_as_string(other.base));
            initRaptor();
        }

        RaptorSerializer::RaptorSerializer(RaptorSerializer &&other) {
            format = other.format;
            namespaces = other.namespaces;
            base = other.base;
            world = other.world;
            serializer = other.serializer;

            other.serializer = NULL;
            other.base = NULL;
            other.world = NULL;
        }

        RaptorSerializer &RaptorSerializer::operator=(const RaptorSerializer &other) {
            format = other.format;
            namespaces = other.namespaces;

            world = raptor_new_world();
            base  = raptor_new_uri(world, raptor_uri_as_string(other.base));
            initRaptor();

            return *this;
        }

        RaptorSerializer &RaptorSerializer::operator=(RaptorSerializer &&other) {
            format = other.format;
            namespaces = other.namespaces;
            serializer = other.serializer;
            base = other.base;
            world = other.world;

            other.serializer = NULL;
            other.base = NULL;
            other.world = NULL;

            return *this;
        }
       */

        void RaptorSerializer::initRaptor() {
            serializer = raptor_new_serializer(world, raptorFormat(format).c_str());
            for(const auto &e : namespaces) {
                raptor_uri* uri = raptor_new_uri(world, (unsigned char const *) e.second.getUri().c_str());
                raptor_serializer_set_namespace(serializer, uri, (unsigned char const *) e.first.c_str());
            }
            raptor_world_set_log_handler(world, this, [](void *user_data, raptor_log_message* message){
                std::cerr << message->level << ": " << message->text << std::endl;
            });
        }

        void RaptorSerializer::prepare(std::ostream &out) {
            stream = raptor_new_iostream_from_handler(world, &out, &raptor_handler);
            raptor_serializer_start_to_iostream(serializer, base, stream);
        }

        void RaptorSerializer::serialize(const rdf::Statement &stmt) {
            raptor_statement* triple = raptor_new_statement(world);

            if (stmt.getMessage().subject().has_uri()) {
                triple->subject = raptor_new_term_from_uri_string(
                        world, (unsigned char const *) stmt.getMessage().subject().uri().uri().c_str());
            } else if (stmt.getMessage().subject().has_bnode()) {
                triple->subject = raptor_new_term_from_blank(
                        world, (unsigned char const *) stmt.getMessage().subject().bnode().id().c_str());
            } else {
                throw SerializationError("invalid subject type");
            }

            triple->predicate = raptor_new_term_from_uri_string(
                    world,  (unsigned char const *) stmt.getMessage().predicate().uri().c_str());

            if (stmt.getMessage().object().has_resource()) {
                const marmotta::rdf::proto::Resource& r = stmt.getMessage().object().resource();
                if (r.has_uri()) {
                    triple->object = raptor_new_term_from_uri_string(
                            world, (unsigned char const *) r.uri().uri().c_str());
                } else if(r.has_bnode()) {
                    triple->object = raptor_new_term_from_blank(
                            world, (unsigned char const *) r.bnode().id().c_str());
                } else {
                    throw SerializationError("invalid object resource type");
                }
            } else if (stmt.getMessage().object().has_literal()) {
                const marmotta::rdf::proto::Literal& l = stmt.getMessage().object().literal();
                if (l.has_stringliteral()) {
                    triple->object = raptor_new_term_from_counted_literal(
                            world,
                            (unsigned char const *) l.stringliteral().content().c_str(), l.stringliteral().content().size(), NULL,
                            (unsigned char const *) l.stringliteral().language().c_str(), l.stringliteral().language().size());
                } else if(l.has_dataliteral()) {
                    triple->object = raptor_new_term_from_counted_literal(
                            world,
                            (unsigned char const *) l.dataliteral().content().c_str(), l.dataliteral().content().size(),
                            raptor_new_uri(world, (unsigned char const *) l.dataliteral().datatype().uri().c_str()),
                            (unsigned char const *) "", 0);
                } else {
                    throw SerializationError("invalid object literal type");
                }
            } else {
                throw SerializationError("invalid object type");
            }

            if (stmt.getMessage().context().has_uri()) {
                    triple->graph = raptor_new_term_from_uri_string(
                            world,  (unsigned char const *) stmt.getMessage().context().uri().uri().c_str());
            } else if (stmt.getMessage().context().has_bnode()) {
                triple->graph = raptor_new_term_from_blank(
                        world, (unsigned char const *) stmt.getMessage().context().bnode().id().c_str());
            } else {
                    throw SerializationError("invalid context type");
            }

            raptor_serializer_serialize_statement(serializer, triple);

            raptor_free_statement(triple);
        }

        void RaptorSerializer::close() {
            raptor_serializer_serialize_end(serializer);
            raptor_free_iostream(stream);
        }

        void ProtoSerializer::prepare(std::ostream &out) {
            out_ = new google::protobuf::io::OstreamOutputStream(&out);
        }

        void ProtoSerializer::serialize(const rdf::Statement &stmt) {
            stmts_.add_statement()->MergeFrom(stmt.getMessage());
        }

        void ProtoSerializer::close() {
            google::protobuf::io::CodedOutputStream* coded_output =
                new google::protobuf::io::CodedOutputStream(out_);
            switch (format) {
                case PROTO:
                    stmts_.SerializeToCodedStream(coded_output);
                    break;
                case PROTO_TEXT:
                    google::protobuf::TextFormat::Print(
                            stmts_, dynamic_cast<google::protobuf::io::ZeroCopyOutputStream*>(out_));
                    break;
            }
            stmts_.Clear();
            delete coded_output;
            delete out_;
        }
    }
}
