//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "io_support.hpp"

namespace path_extend {

template<class Graph>
using EdgeNamingF = std::function<std::string (const Graph&, EdgeId)>;

template<class Graph>
EdgeNamingF<Graph> IdNamingF(const string &prefix = "") {
    return [=](const Graph &g, EdgeId e) {
        return io::MakeContigId(g.int_id(e), prefix);
    };
}

template<class Graph>
EdgeNamingF<Graph> BasicNamingF(const string &prefix = "EDGE") {
    return [=](const Graph &g, EdgeId e) {
        return io::MakeContigId(g.int_id(e), g.length(e) + g.k(), g.coverage(e), prefix);
    };
}

template<class Graph>
class CanonicalEdgeHelper {
    const Graph &g_;
    const EdgeNamingF<Graph> naming_f_;
    const string pos_orient_;
    const string neg_orient_;
public:

    CanonicalEdgeHelper(const Graph &g,
                        EdgeNamingF<Graph> naming_f = IdNamingF<Graph>(),
                        const string& pos_orient = "+",
                        const string& neg_orient = "-") :
            g_(g), naming_f_(naming_f),
            pos_orient_(pos_orient), neg_orient_(neg_orient) {
    }

    bool IsCanonical(EdgeId e) const {
        return e <= g_.conjugate(e);
    }

    EdgeId Canonical(EdgeId e) const {
        return IsCanonical(e) ? e : g_.conjugate(e);
    }

    std::string GetOrientation(EdgeId e) const {
        return IsCanonical(e) ? pos_orient_ : neg_orient_;
    }

    std::string EdgeOrientationString(EdgeId e,
                                      const std::string &delim = "") const {
        return naming_f_(g_, Canonical(e)) + delim + GetOrientation(e);
    }

    std::string EdgeString(EdgeId e) const {
        VERIFY(IsCanonical(e));
        return naming_f_(g_, e);
    }
};

template<class Graph>
class FastgWriter {
    typedef typename Graph::EdgeId EdgeId;
    const Graph &graph_;
    CanonicalEdgeHelper<Graph> short_namer_;
    CanonicalEdgeHelper<Graph> extended_namer_;

    string ToPathString(const BidirectionalPath &path) const {
        if (path.Empty())
            return "";
        string res = short_namer_.EdgeOrientationString(path.Front());
        for (size_t i = 1; i < path.Size(); ++i) {
            if (graph_.EdgeEnd(path[i - 1]) != graph_.EdgeStart(path[i]) || path.GapAt(i).gap > 0) {
                res += ";\n" + short_namer_.EdgeOrientationString(path[i]);
            } else {
                res += "," + short_namer_.EdgeOrientationString(path[i]);
            }
        }
        return res;
    }

    string FormHeader(const string &id,
                      const set<string>& next_ids) {
        std::stringstream ss;
        ss << id;
        if (next_ids.size() > 0) {
            auto delim = ":";
            for (const auto &s : next_ids) {
                ss  << delim << s;
                delim = ",";
            }
        }
        ss << ";";
        return ss.str();
    }

public:

    FastgWriter(const Graph &graph,
                EdgeNamingF<Graph> edge_naming_f = BasicNamingF<Graph>())
            : graph_(graph),
              short_namer_(graph_),
              extended_namer_(graph_, edge_naming_f, "", "'") {
    }

    void WriteSegmentsAndLinks(const string &fn) {
        io::OutputSequenceStream os(fn);
        for (auto it = graph_.ConstEdgeBegin(); !it.IsEnd(); ++it) {
            EdgeId e = *it;
            set<string> next;
            for (EdgeId next_e : graph_.OutgoingEdges(graph_.EdgeEnd(e))) {
                next.insert(extended_namer_.EdgeOrientationString(next_e));
            }
            os << io::SingleRead(FormHeader(extended_namer_.EdgeOrientationString(e), next),
                                 graph_.EdgeNucls(e).str());
        }
    }

    void WritePaths(const ScaffoldStorage &scaffold_storage, const string &fn) const {
        std::ofstream os(fn);
        for (const auto& scaffold_info : scaffold_storage) {
            os << scaffold_info.name << "\n";
            os << ToPathString(*scaffold_info.path) << "\n";
            os << scaffold_info.name << "'" << "\n";
            os << ToPathString(*scaffold_info.path->GetConjPath()) << "\n";
        }
    }

};

template<class Graph>
class GFAWriter {
    typedef typename Graph::EdgeId EdgeId;
    const Graph &graph_;
    CanonicalEdgeHelper<Graph> edge_namer_;
    std::ostream &os_;

    void WriteSegment(const std::string& edge_id, const Sequence &seq, double cov) {
        os_ << "S\t" << edge_id << "\t"
            << seq.str() << "\t"
            << "KC:i:" << size_t(math::round(cov)) << "\n";
    }

    void WriteSegments() {
        for (auto it = graph_.ConstEdgeBegin(true); !it.IsEnd(); ++it) {
            EdgeId e = *it;
            WriteSegment(edge_namer_.EdgeString(e), graph_.EdgeNucls(e),
                         graph_.coverage(e) * double(graph_.length(e)));
        }
    }

    void WriteLink(EdgeId e1, EdgeId e2,
                   size_t overlap_size) {
        os_ << "L\t" << edge_namer_.EdgeOrientationString(e1, "\t") << "\t"
            << edge_namer_.EdgeOrientationString(e2, "\t") << "\t"
            << overlap_size << "M\n";
    }

    void WriteLinks() {
        //TODO switch to constant vertex iterator
        for (auto it = graph_.SmartVertexBegin(/*canonical only*/true); !it.IsEnd(); ++it) {
            VertexId v = *it;
            for (auto inc_edge : graph_.IncomingEdges(v)) {
                for (auto out_edge : graph_.OutgoingEdges(v)) {
                    WriteLink(inc_edge, out_edge, graph_.k());
                }
            }
        }
    }

    void WritePath(const std::string& name, size_t segment_id, const vector<std::string> &edge_strs) {
        os_ << "P" << "\t" ;
        os_ << name << "_" << segment_id << "\t";
        std::string delimeter = "";
        for (const auto& e : edge_strs) {
            os_ << delimeter << e;
            delimeter = ",";
        }
        os_ << "\t*\n";
//        delimeter = "";
//        for (size_t i = 0; i < edge_strs.size() - 1; ++i) {
//            os_ << delimeter << "*";
//            delimeter = ",";
//        }
//        os_ << "\n";
    }

public:
    GFAWriter(const Graph &graph, std::ostream &os,
              EdgeNamingF<Graph> naming_f = IdNamingF<Graph>())
            : graph_(graph),
              edge_namer_(graph_, naming_f),
              os_(os) {
    }

    void WriteSegmentsAndLinks() {
        WriteSegments();
        WriteLinks();
    }

    void WritePaths(const ScaffoldStorage &scaffold_storage) {
        for (const auto& scaffold_info : scaffold_storage) {
            const path_extend::BidirectionalPath &p = *scaffold_info.path;
            if (p.Size() == 0) {
                continue;
            }
            std::vector<std::string> segmented_path;
            //size_t id = p.GetId();
            size_t segment_id = 1;
            for (size_t i = 0; i < p.Size() - 1; ++i) {
                EdgeId e = p[i];
                segmented_path.push_back(edge_namer_.EdgeOrientationString(e));
                if (graph_.EdgeEnd(e) != graph_.EdgeStart(p[i+1]) || p.GapAt(i+1).gap > 0) {
                    WritePath(scaffold_info.name, segment_id, segmented_path);
                    segment_id++;
                    segmented_path.clear();
                }
            }

            segmented_path.push_back(edge_namer_.EdgeOrientationString(p.Back()));
            WritePath(scaffold_info.name, segment_id, segmented_path);
        }
    }

};

typedef std::function<void (const ScaffoldStorage&)> PathsWriterT;

class ContigWriter {
    const Graph& g_;
    shared_ptr<ContigNameGenerator> name_generator_;

public:

    static void WriteScaffolds(const ScaffoldStorage &scaffold_storage, const string &fn) {
        io::OutputSequenceStream oss(fn);
        std::ofstream os_fastg;

        for (const auto& scaffold_info : scaffold_storage) {
            TRACE("Scaffold " << scaffold_info.name << " originates from path " << scaffold_info.path->str());
            oss << io::SingleRead(scaffold_info.name, scaffold_info.sequence);
        }
    }

    static PathsWriterT BasicFastaWriter(const string &fn) {
        return [=](const ScaffoldStorage& scaffold_storage) {
            WriteScaffolds(scaffold_storage, fn);
        };
    }

    ContigWriter(const Graph& g,
                 shared_ptr<ContigNameGenerator> name_generator) :
            g_(g),
            name_generator_(name_generator) {
    }

    void OutputPaths(const PathContainer &paths, const vector<PathsWriterT>& writers) const;

    void OutputPaths(const PathContainer &paths, PathsWriterT writer) const {
        OutputPaths(paths, vector<PathsWriterT>{writer});
    }

    void OutputPaths(const PathContainer &paths, const string &fn) const {
        OutputPaths(paths, BasicFastaWriter(fn));
    }

private:
    DECL_LOGGER("ContigWriter")
};

}
