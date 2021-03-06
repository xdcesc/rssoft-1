/*
 Copyright 2013 Edouard Griffiths <f4exb at free dot fr>

 This file is part of CCSoft. A Convolutional Codes Soft Decoding library

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Boston, MA  02110-1301  USA

 Convolutional soft-decision decoder based on the stack or Zigangirov-Jelinek
 (ZJ) algorithm. Uses the node+edge combination in the code tree.

 Uses fixed arrays

 */
#ifndef __CC_STACK_DECODING_FA_H__
#define __CC_STACK_DECODING_FA_H__

#include "CC_SequentialDecoding_FA.h"
#include "CC_SequentialDecodingInternal_FA.h"
#include "CC_Encoding_FA.h"
#include "CCSoft_Exception.h"
#include "CC_TreeNodeEdge_FA.h"
#include "CC_ReliabilityMatrix.h"

#include <cmath>
#include <map>
#include <algorithm>
#include <iostream>


namespace ccsoft
{

/**
 * \brief The Stack Decoding class with node+edge combination
 * This version uses fixed arrays to store registers and forward node+edges pointers.
 * N_k template parameter gives the size of the input symbol (k parameter) and therefore the number of registers.
 * There are (1<<N_k) forward node+edges.
 * \tparam T_Register Type of the encoder internal registers
 * \tparam T_IOSymbol Type of the input and output symbols
 * \tparam N_k Input symbol size in bits (k parameter)
 * \tparam N_k Size of an input symbol in bits (k parameter)
 */
template<typename T_Register, typename T_IOSymbol, unsigned int N_k>
class CC_StackDecoding_FA : public CC_SequentialDecoding_FA<T_Register, T_IOSymbol, N_k>, public CC_SequentialDecodingInternal_FA<T_Register, T_IOSymbol, CC_TreeNodeEdgeTag_Empty, N_k>
{
public:
    /**
     * Constructor
     * \param constraints Vector of register lengths (constraint length + 1). The number of elements determines k.
     * \param genpoly_representations Generator polynomial numeric representations. There are as many elements as there
     * are input bits (k). Each element is itself a vector with one polynomial value per output bit. The smallest size of
     * these vectors is retained as the number of output bits n. The input bits of a symbol are clocked simultaneously into
     * the right hand side, or least significant position of the internal registers. Therefore the given polynomial representation
     * of generators should follow the same convention.
     */
	CC_StackDecoding_FA(const std::vector<unsigned int>& constraints,
            const std::vector<std::vector<T_Register> >& genpoly_representations) :
                CC_SequentialDecoding_FA<T_Register, T_IOSymbol, N_k>(constraints, genpoly_representations),
                CC_SequentialDecodingInternal_FA<T_Register, T_IOSymbol, CC_TreeNodeEdgeTag_Empty, N_k>()
    {}

    /**
     * Destructor. Does a final garbage collection
     */
    virtual ~CC_StackDecoding_FA()
    {}

    /**
     * Reset the decoding process
     */
    void reset()
    {
        ParentInternal::reset();
        Parent::reset();
        node_edge_stack.clear();
    }

    /**
     * Get the score at the top of the stack. Valid anytime the process has started (stack not empty).
     */
    float get_stack_score() const
    {
        return node_edge_stack.begin()->first.path_metric;
    }

    /**
     * Get the stack size
     */
    unsigned int get_stack_size() const
    {
        return node_edge_stack.size();
    }

    /**
     * Decodes given the reliability matrix
     * \param relmat Reference to the reliability matrix
     * \param decoded_message Vector of symbols of retrieved message
     */
    virtual bool decode(const CC_ReliabilityMatrix& relmat, std::vector<T_IOSymbol>& decoded_message)
    {
        if (relmat.get_message_length() < Parent::encoding.get_m())
        {
            throw CCSoft_Exception("Reliability Matrix should have a number of columns at least equal to the code constraint");
        }

        if (relmat.get_nb_symbols_log2() != Parent::encoding.get_n())
        {
            throw CCSoft_Exception("Reliability Matrix is not compatible with code output symbol size");
        }

        reset();
        ParentInternal::init_root(); // initialize the root node
        Parent::node_count++;
        visit_node_forward(ParentInternal::root_node, relmat); // visit the root node

        // loop until we get to a terminal node or the metric limit is encountered hence the stack is empty
        while ((node_edge_stack.size() > 0)
            && (node_edge_stack.begin()->second->get_depth() < relmat.get_message_length() - 1))
        {
            StackNodeEdge* node = node_edge_stack.begin()->second;
            //std::cout << std::dec << node->get_id() << ":" << node->get_depth() << ":" << node_stack.begin()->first.path_metric << std::endl;
            visit_node_forward(node, relmat);

            if ((Parent::use_node_limit) && (Parent::node_count > Parent::node_limit))
            {
                std::cerr << "Node limit exhausted" << std::endl;
                return false;
            }
        }

        // Top node has the solution if we have not given up
        if (!Parent::use_metric_limit || node_edge_stack.size() != 0)
        {
            //std::cout << "final: " << std::dec << node_stack.begin()->second->get_id() << ":" << node_stack.begin()->second->get_depth() << ":" << node_stack.begin()->first.path_metric << std::endl;
            ParentInternal::back_track(node_edge_stack.begin()->second, decoded_message, true); // back track from terminal node to retrieve decoded message
            Parent::codeword_score = node_edge_stack.begin()->first.path_metric; // the codeword score is the path metric
            return true;
        }
        else
        {
            std::cerr << "Metric limit encountered" << std::endl;
            return false; // no solution
        }
    }

    /**
     * Print stats to an output stream
     * \param os Output stream
     * \param success True if decoding was successful
     */
    virtual void print_stats(std::ostream& os, bool success)
    {
        std::cout << "score = " << Parent::get_score()
                << " stack_score = " << get_stack_score()
                << " #nodes = " << Parent::get_nb_nodes()
                << " stack_size = " << get_stack_size()
                << " max depth = " << Parent::get_max_depth();
    }

    /**
     * Print stats summary to an output stream
     * \param os Output stream
     * \param success True if decoding was successful
     */
    virtual void print_stats_summary(std::ostream& os, bool success)
    {
        std::cout << "_RES " << (success ? 1 : 0) << ","
                << Parent::get_score() << ","
                << get_stack_score() << ","
                << Parent::get_nb_nodes() << ","
                << get_stack_size() << ","
                << Parent::get_max_depth();
    }

    /**
     * Print the dot (Graphviz) file of the current decode tree to an output stream
     * \param os Output stream
     */
    virtual void print_dot(std::ostream& os)
    {
        ParentInternal::print_dot_internal(os);
    }

protected:
    typedef CC_SequentialDecoding_FA<T_Register, T_IOSymbol, N_k> Parent;                                       //!< Parent class this class inherits from
    typedef CC_SequentialDecodingInternal_FA<T_Register, T_IOSymbol, CC_TreeNodeEdgeTag_Empty, N_k> ParentInternal; //!< Parent class this class inherits from
    typedef CC_TreeNodeEdge_FA<T_IOSymbol, T_Register, CC_TreeNodeEdgeTag_Empty, N_k> StackNodeEdge; //!< Class of code tree nodes in the stack algorithm

    /**
     * Visit a new node
     * \node Node+edge combo to visit
     * \relmat Reliability matrix being used
     */
    virtual void visit_node_forward(CC_TreeNodeEdge_FA<T_IOSymbol, T_Register, CC_TreeNodeEdgeTag_Empty, N_k>* node_edge, const CC_ReliabilityMatrix& relmat)
    {
        int forward_depth = node_edge->get_depth() + 1;
        T_IOSymbol out_symbol;
        T_IOSymbol end_symbol;

        // return encoder to appropriate state
        if (node_edge->get_depth() >= 0) // does not concern the root node
        {
            Parent::encoding.set_registers(node_edge->get_registers());
        }

        if ((Parent::tail_zeros) && (forward_depth > relmat.get_message_length()-Parent::encoding.get_m()))
        {
            end_symbol = 1; // if zero tail option assume tail symbols are all zeros
        }
        else
        {
            end_symbol = (1<<Parent::encoding.get_k()); // full scan all possible input symbols
        }

        // loop through assumption for this symbol place
        for (T_IOSymbol in_symbol = 0; in_symbol < end_symbol; in_symbol++)
        {
            Parent::encoding.encode(in_symbol, out_symbol, in_symbol > 0); // step only for a new symbol place
            float edge_metric = ParentInternal::log2(relmat(out_symbol, forward_depth)) - Parent::edge_bias;

            float forward_path_metric = edge_metric + node_edge->get_path_metric();
            if ((!Parent::use_metric_limit) || (forward_path_metric > Parent::metric_limit))
            {
                StackNodeEdge *next_node_edge = new StackNodeEdge(Parent::node_count, node_edge, in_symbol, edge_metric, forward_path_metric, forward_depth);
                next_node_edge->set_registers(Parent::encoding.get_registers());
                node_edge->set_outgoing_node_edge(next_node_edge, in_symbol); // add forward edge+node combo
                node_edge_stack[NodeEdgeOrdering(forward_path_metric, Parent::node_count)] = next_node_edge;
                //std::cout << "->" << std::dec << node_count << ":" << forward_depth << " (" << (unsigned int) in_symbol << "," << (unsigned int) out_symbol << "): " << forward_path_metric << std::endl;
                Parent::node_count++;
            }
        }

        Parent::cur_depth = forward_depth; // new encoder position

        if (Parent::cur_depth > Parent::max_depth)
        {
            Parent::max_depth = Parent::cur_depth;
        }

        if (node_edge->get_depth() >= 0)
        {
            remove_node_from_stack(node_edge); // remove current node from the stack unless it is the root node which is not in the stack
        }
    }

    /**
     * Removes a node from the stack map. Does a full scan but usually the nodes to be removed are on the top of the stack (i.e. beginning of the map).
     */
    void remove_node_from_stack(StackNodeEdge* node_edge)
    {
        typename std::map<NodeEdgeOrdering, StackNodeEdge*, std::greater<NodeEdgeOrdering> >::iterator stack_it = node_edge_stack.begin();

        for (; stack_it != node_edge_stack.end(); ++stack_it)
        {
            if (node_edge == stack_it->second)
            {
                node_edge_stack.erase(stack_it);
                break;
            }
        }
    }

    std::map<NodeEdgeOrdering, StackNodeEdge*, std::greater<NodeEdgeOrdering> > node_edge_stack; //!< Ordered stack of node+edge combos by decreasing path metric
};

} // namespace ccsoft

#endif
