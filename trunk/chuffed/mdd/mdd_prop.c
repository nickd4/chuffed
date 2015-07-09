#include "mdd_prop.h"

#include <iostream>
#include <cstring>
#include <climits>

#include "support/misc.h"

#include "MDD.h"

#include "opts.h"

// #define TIKZDEBUG

// #define EXPNOGOOD
// #define WEAKNOGOOD
// #define INCREASON

// #define SORTEDGES
#ifdef SORTEDGES
#define SORTOP <=
//#define SORTOP >=
#endif

#define MAXTIME (3<<(sizeof(unsigned int)-3))

#define IS_DEAD(e)          ((e)->kill_flags)
#define WATCHED_ABOVE(e)    (((e)->watch_flags)&2)
#define WATCHED_BELOW(e)    (((e)->watch_flags)&1)
#define WATCHED_VAL(e)    (((e)->watch_flags)&4)

#define KILLED_DOM_IND(n)   (edges[(n)].kill_flags&4)

#define IS_DEAD_IND(n)      (edges[(n)].kill_flags)
#define IS_DEAD_LIM(n,lim)  ((edges[(n)].kill_flags)!= 0 && edges[(n)].kill_flags < lim)

#define WATCHED_VALoAB(e)   (((e)->watch_flags)&6)
#define WATCHED_VALoBE(e)   (((e)->watch_flags)&5)

#if 0
#define SET_WATCH_BE(e)     (((e)->watch_flags)|=1)
#define SET_WATCH_AB(e)     (((e)->watch_flags)|=2)
#define SET_WATCH_VAL(e)    (((e)->watch_flags)|=4)
#define CLEAR_WATCH_BE(e)   (((e)->watch_flags)&=6)
#define CLEAR_WATCH_AB(e)   (((e)->watch_flags)&=5)
#define CLEAR_WATCH_VAL(e)  (((e)->watch_flags)&=3)
#else
#define SET_WATCH_BE(n)     ((edges[(n)].watch_flags)|=1)
#define SET_WATCH_AB(n)     ((edges[(n)].watch_flags)|=2)
#define SET_WATCH_VAL(n)    ((edges[(n)].watch_flags)|=4)
#define CLEAR_WATCH_BE(n)   ((edges[(n)].watch_flags)&=6)
#define CLEAR_WATCH_AB(n)   ((edges[(n)].watch_flags)&=5)
#define CLEAR_WATCH_VAL(n)  ((edges[(n)].watch_flags)&=3)
#endif


#define OUT_EDGES(n)        (node_edges + nodes[(n)].out_start)
#define OUT_END(n)          (node_edges + nodes[(n)].out_start + nodes[(n)].num_out)
#define IN_EDGES(n)         (node_edges + nodes[(n)].in_start)
#define IN_END(n)           (node_edges + nodes[(n)].in_start + nodes[(n)].num_in)

// New stuff.
#define VAL(v)              (val_entries[(v)])
// #define VAL_EDGES(val)      (val_entries[(val)].first_edge)
#define VAL_EDGES(val)      (val_edges + val_entries[(val)].first_off)
#define VAL_CACHE(val)      ((val_entries[(val)].search_cache) ? val_entries[(val)].search_cache : (VAL_EDGES(val) + 1))
// #define VAL_END(val)        (val_entries[(val)].first_edge + val_entries[(val)].count)
#define VAL_END(val)        (val_edges + val_entries[(val)].first_off + val_entries[(val)].count)

template <class T>
std::ostream& operator<< (std::ostream& out, vec<T>& list)
{
    out << "[";
    for( int i = 0; i < list.size(); i++ )
    {
        if( i > 0 )
            out << ",";
        out << list[i];
    }
    out << "]";
    return out;
}

#ifdef INSTRUMENT
static unsigned int propnum = 0;

extern long long int node_visits;
#endif

vec<int> kfa;
vec<int> kfb;


void MDDCompile(MDDTable& t, MDD root,
          vec<int>& domain_sizes,
          vec<val_entry>& val_entries, vec<inc_node>& inc_nodes,
          vec<inc_edge>& edge_arr,
          vec<int>& val_edges, vec<int>& node_edges);

template<int U>
MDDProp<U>* MDDProp_new(MDDTemplate* _templ, vec<IntView<U> >& _intvars)
{
#if 0
    void* mem = malloc(sizeof(MDDProp) +
                    sizeof(inc_node)*((_prop->numNodes())-1));
    return new (mem) MDDProp(_prop,_id,_sat_vars,_priority);
#else
    return new MDDProp<U>(_templ,_intvars);
#endif
}

template<int U>
MDDProp<U>::MDDProp(MDDTemplate* _templ, vec<IntView<U> >& _intvars, bool _simple) :
    simple( _simple ), fixedvars(_templ->_val_entries.size()), clear_queue()
{
   assert( _intvars.size() == _templ->_doms.size() );

   // Larger domain stuff
   kfa.reserve(_templ->_mdd_nodes.size());
   kfb.reserve(_templ->_mdd_nodes.size()); 
   
   _templ->_val_entries.copyTo(val_entries);
   _templ->_mdd_nodes.copyTo(nodes);
   _templ->_edges.copyTo(edges);
   
   _intvars.copyTo(intvars);
   
   for( int i = 0; i < intvars.size(); i++ )
   {
      for( int j = 0; j < _templ->_doms[i]; j++ )
      {
//         assert( intvars[i].getMin() <= j && j <= intvars[i].getMax() );
         boolvars.push(intvars[i].getLit(j,1)); // v[i] \eq j
//         attach(boolvars.last(), EVENT_U);
		 boolvars.last().attach(this, boolvars.size()-1, EVENT_U);
      }
      if( _templ->_doms[i] == 1 )
      {
         if( intvars[i].setValNotR(0) )
            intvars[i].setVal(0);
      }
   }

   // Static propagation.
//   assert( val_entries.size() == boolvars.size() );
   for( int i = 0; i < val_entries.size(); i++ )
   {
      if( val_entries[i].count == 0 )
      {
         if (intvars[val_entries[i].var].remValNotR(val_entries[i].val))
         {
            if(!intvars[val_entries[i].var].remVal(val_entries[i].val))
                ERROR("Failure in static propagation.");
         }

         fixedvars.insert(i);
         val_entries[i].val_lim = 0;
      }
   }

   _templ->_node_edges.copyTo(node_edges);
   _templ->_val_edges.copyTo(val_edges);

   // Attach to the solver.  
   priority = 1; 

 #ifdef TIKZDEBUG   
   debugStateTikz(0);
 #endif

   assert(val_entries.size() == boolvars.size());
   
   if( clear_queue.size() > 0 )
      pushInQueue(); 
}

template<int U>
void MDDProp<U>::static_inference(vec<int>& inferences)
{    
    val_entry* val(val_entries);
    int end(val_entries.size());
    for( int i = 0; i < end; val++, i++ )
    {
        if( val->count == 0 )
            inferences.push(i);
    }
}

template<int U>
void MDDProp<U>::genReason(vec<int>& out, Value value)
{
    out.clear();
    
    int lim;
    if( value != -1 )
    {
        out.push(value);
        lim = val_entries[value].val_lim;
    } else {
        lim = fixedvars.size();
    }

    if( simple )
    {
        val_entry* valent = val_entries;
        int end = val_entries.size();
        for( int i = 0; i < end; valent++, i++ )
        {
            if( fixedvars.elemLim(lim,i) )
            {
                assert( !intvars[val_entries[i].var].indomain(val_entries[i].val) );
                out.push(i);
            }
        }
    } else {
#ifdef INCREASON
      if( value == -1 )
      {
          fullConstructReason(lim,out,value);
      } else {
          incConstructReason(lim,out,value);
      }
#else
      fullConstructReason(lim,out,value);
#endif
    }

#if 1
    // Assignment order
    int slim = (value == -1) ? 1 : 2;
    for( int i = slim; i < out.size(); i++ )
    {
        int val = out[i];
        int vlim = fixedvars.pos(val);
        int j;
        for( j = i;
                j >= slim && vlim > static_cast<int>(fixedvars.pos(out[j-1])); j-- )
//                j >= slim && ( vlim > val_entries[out[j-1]].val_lim ||
//                        (vlim == val_entries[out[j-1]].val_lim && val < out[j-1] )); j-- )
        {
            out[j] = out[j-1];
        }
        out[j] = val;
    }
#endif
#if 0
    // Increasing order
    int slim = (value == -1) ? 1 : 2;
    for( int i = slim; i < out.size(); i++ )
    {
        int val = out[i];
        int j;
        for( j = i;
                j >= slim && val < out[j-1]; j-- )
        {
            out[j] = out[j-1];
        }
        out[j] = val;
    }
#endif
#if 0
    // Decreasing order
    int slim = (value == -1) ? 1 : 2;
    for( int i = slim; i < out.size(); i++ )
    {
        int val = out[i];
        int j;
        for( j = i;
                j >= slim && val > out[j-1]; j-- )
        {
            out[j] = out[j-1];
        }
        out[j] = val;
    }
#endif
#if 0
    // Random order
    int slim = (value == -1) ? 1 : 2;
    for( int i = slim; i < out.size(); i++ )
    {
        int j = i + (rand() % (out.size() - i));
        int temp = out[i];
        out[i] = out[j];
        out[j] = temp;
    }
#endif

#ifdef INSTRUMENT
    if( prop )
        (prop->explncount)++;
#endif

//    NOT_SUPPORTED;
}

template<int U>
void MDDProp<U>::shrinkReason(vec<int>& out, Value value, int threshold)
{
//    if( value == -1 )
//        return;

    int off = value == -1 ? 0 : 1;

    for( int k = 0; k < val_entries.size(); k++ )
       val_entries[k].stat_flag = 0;
    for( int k = off; k < out.size(); k++ )
       val_entries[out[k]].stat_flag = 1;
    int lcount = 0;
    int tcount = 0;
    int nl = 0;

#if 1
    int temp = out[0];
    out.clear();
    out.push(temp);

    for( int v = 0; v < val_entries.size(); v++ )
    {
        assert( val_entries[v].stat_flag >= 0 && val_entries[v].stat_flag <= 1 );
        lcount += val_entries[v].stat_flag;
        if( val_entries[v].count > 0 )
            tcount++;

        assert( v == val_entries.size() - 1 || val_entries[v].var <= val_entries[v+1].var );
        if( v == val_entries.size() - 1 || val_entries[v].var != val_entries[v+1].var )
        {
            if( lcount == tcount - 1 && lcount > 0 )
            {
                for( int m = v ; m >= 0 && val_entries[m].var == val_entries[v].var; m-- )
                    val_entries[m].stat_flag = (val_entries[m].stat_flag || val_entries[m].count == 0) ? 0 : -1;
            }
            tcount = 0;
            lcount = 0;
        }
    }
#endif

    for( int v = 0; v < val_entries.size(); v++ )
    {
        assert( val_entries[v].stat_flag >= -1 && val_entries[v].stat_flag <= 1 );

        if( val_entries[v].stat_flag )
        {
            out.push( (val_entries[v].stat_flag*(v+1)) - 1 );
//            out.push( v );
            val_entries[v].stat_flag = 0;
        }
    }
    if( nl )
        fprintf(stderr,"\n");
   
}

template<int U>
void MDDProp<U>::debugStateTikz(unsigned int lim, bool debug)
{
    vec< vec<int> > var_nodes;
    
    FILE* out = stdout;

    fprintf(out,"\\begin{tikzpicture}\n");
    fprintf(out,"\\tikzstyle{vertex}=[draw,circle,fill=black!25,minimum size=20pt,inner sep=0pt]\n");
    fprintf(out,"\\tikzstyle{smallvert}=[circle,fill=black!25,minimum size=5pt,inner sep=0pt]\n");
    fprintf(out,"\\tikzstyle{edge} = [draw,thick,->]\n");
    fprintf(out,"\\tikzstyle{kdedge} = [draw,thick,=>,color=red]\n");
    fprintf(out,"\\tikzstyle{kaedge} = [draw,thick,=>,color=blue]\n");
    fprintf(out,"\\tikzstyle{kbedge} = [draw,thick,=>,color=pinegreen!25]\n");
    
    int maxw = 0;
    int maxvar = nodes[0].var;

    for( int i = 0; i < nodes.size(); i++ )
    {
        inc_node node = nodes[i];
        while( var_nodes.size() <= node.var )
            var_nodes.push();

        var_nodes[node.var].push(i);

        if( var_nodes[node.var].size() > maxw )
            maxw = var_nodes[node.var].size();
    }
    
    fprintf(out,"\\foreach \\pos/\\name/\\stat in \{");
    
    bool first = true;
    for( int i = 0; i < var_nodes.size(); i++ )
    {
        int off;
        off = maxw - var_nodes[i].size() + 1;

        for( int j = 0; j < var_nodes[i].size(); j++ )
        {
            if(first)
            {
                first = false;
            } else {
                fprintf(out,",");
            }
            
            if(debug)
            {
                fprintf(out,"{(%d,%f)/%d/%d}",off,1.5*(maxvar - i),var_nodes[i][j],nodes[var_nodes[i][j]].kill_flag);
            } else {
                fprintf(out,"{(%d,%f)/%d/%d}",off,1.5*(maxvar - i),var_nodes[i][j],i);
            }
            off += 2;
        }
    }

    if (debug) {
        fprintf(out,"}\n\t\t\\node[vertex] (\\name) at \\pos {$\\name (\\stat)$};\n");
    } else {
        fprintf(out,"}\n\t\t\\node[vertex] (\\name) at \\pos {$x_{\\stat}$};\n");
    }


    fprintf(out,"\\foreach \\source/\\dest/\\label in \{");
    
    first = true;
    for( int i = 0; i < edges.size(); i++ )
    {
        if( (edges[i].kill_flags) == 0 || (edges[i].kill_flags) > ((lim<<3)|7) )
        {
            if(first)
            {
                first = false;
            } else {
                fprintf(out,",");
            }
            
            if( debug )
            {
                fprintf(out,"{%d/%d/%d}",edges[i].begin,edges[i].end,edges[i].val);
            } else {
                int valtemp = edges[i].val;
                int evar = val_entries[valtemp].var;
                int tval = 0;

                while( --valtemp > 0 && val_entries[valtemp].var == evar )
                    tval++;

                fprintf(out,"{%d/%d/%d}",edges[i].begin,edges[i].end,tval);
            }
        }
    }
    fprintf(out,"}\n\t\t\\path[edge] (\\source) -- node {$\\label$} (\\dest);\n");

    fprintf(out,"\\foreach \\source/\\dest/\\label in \{");
    first = true;
    for( int i = 0; i < edges.size(); i++ )
    {
        if( (edges[i].kill_flags) < ((lim<<3)|7) && ((edges[i].kill_flags)&1) )
        {
            if(first)
            {
                first = false;
            } else {
                fprintf(out,",");
            }
    
            fprintf(out,"{%d/%d/%d}",edges[i].begin,edges[i].end,edges[i].val);
        }
    }
    fprintf(out,"}\n\t\t\\path[kaedge] (\\source) -- node {$\\label$} (\\dest);\n");

    fprintf(out,"\\foreach \\source/\\dest/\\label in \{");
    first = true;
    for( int i = 0; i < edges.size(); i++ )
    {
        if( (edges[i].kill_flags) < ((lim<<3)|7) && ((edges[i].kill_flags)&2) )
        {
            if(first)
            {
                first = false;
            } else {
                fprintf(out,",");
            }
    
            fprintf(out,"{%d/%d/%d}",edges[i].begin,edges[i].end,edges[i].val);
        }
    }
    fprintf(out,"}\n\t\t\\path[kbedge] (\\source) -- node {$\\label$} (\\dest);\n");

    fprintf(out,"\\foreach \\source/\\dest/\\label in \{");
    first = true;
    for( int i = 0; i < edges.size(); i++ )
    {
        if( (edges[i].kill_flags) < ((lim<<3)|7) && ((edges[i].kill_flags)&4) )
        {
            if(first)
            {
                first = false;
            } else {
                fprintf(out,",");
            }
    
            fprintf(out,"{%d/%d/%d}",edges[i].begin,edges[i].end,edges[i].val);
        }
    }
    fprintf(out,"}\n\t\t\\path[kdedge] (\\source) -- node {$\\label$} (\\dest);\n");



    fprintf(out,"\\end{tikzpicture}\n");
}

// Incrementally constructs a (non-minimal) reason.
template<int U>
void MDDProp<U>::incConstructReason(unsigned int lim, vec<int>& out, Value val, int threshold)
{
#ifdef TIKZDEBUG
    static int run_count = 0;
    if( run_count == 0 )
    {
        fprintf(stderr,"Generating reason for $v(%d) \\neq %d.$\n",val_entries[val].var,val);
        debugStateTikz(lim);
    }
#endif

    lim = (lim<<3)|7;
    kfa.clear();
    kfb.clear();
    
    int* edge;
    int* end;

    edge = VAL_EDGES(val);
    end = VAL_END(val);

    for(; edge != end; edge++)
    {
        inc_edge* e = edges + *edge;
        assert(IS_DEAD(e));
        assert(!((e->kill_flags)&4));

        if( (e->kill_flags)&1 )
        {
            assert( ((e->kill_flags)&7) == 1 );
            // We need to explain why this was killed from above
            kfa.push(e->begin);
        } else {
            assert( ((e->kill_flags)&7) == 2 );
            // Same, but killed from below
            kfb.push(e->end);
        }
    }
    
    int i = 0;
    int j;
    while( i < kfa.size() )
    {
#ifdef WEAKNOGOOD
        int resbegin = out.size();
#endif
        for( j = i; j < kfa.size(); j++ )
        {
            edge = IN_EDGES(kfa[j]);
            end = IN_END(kfa[j]);
            
            // Process the incoming edges
            for(; edge != end; edge++ )
            {
                assert( IS_DEAD_LIM(*edge,lim) );
                assert( !((edges[*edge].kill_flags)&2) );

                inc_edge& e(edges[*edge]);
                inc_node& next(nodes[e.begin]);

#ifdef USE_WATCHES                
                int nsupp( node_edges[next.out_start] );
                if( (e.kill_flags)&4 &&
                    (!IS_DEAD_LIM(nsupp,lim) || !((next.kill_flag)&1) ) )
#else
//                if( (e.kill_flags)&4 )
                if( (e.kill_flags)&4 &&
                    (!(next.count_in == 0 && next.kill_flag < lim) || !((next.kill_flag)&1) ) )
#endif
                {
                    if( !val_entries[e.val].stat_flag )
                    {
                        if( !IS_DEAD_LIM(val_edges[val_entries[e.val].first_off],lim) )
                        {
//                            fprintf(stderr,"ARRGH: %d (%d)\n",e.val,vars[e.val].var);
                            fprintf(stderr,"ARRGH: %d\n",e.val);
                        }
                        
                        // assert( IS_DEAD_LIM(val_entries[e.val].first_off,lim) );
                        out.push(e.val);
                        val_entries[e.val].stat_flag = 1;
                    }

                } else {
                    next.stat_flag = 1;
                }
            }
        }

#ifdef WEAKNOGOOD
        if( out.size() - resbegin >= threshold && intvars[nodes[kfa[i]].var - 1].isFixed() )
        {
            // Check that the variable is fixed,
            // and if so rewrite the reason.
            int count = 0;
            int cvar = nodes[kfa[i]].var-1;
            
            // Hideously expensive.
            for( int k = 0; k < val_entries.size() ; k++ )
            {
                if( val_entries[k].count > 0 && val_entries[k].var == cvar)
                {
                    if( !fixedvars.elemLim(lim>>3,k) )
                        count++;
                }
            }


            if( count == 1 )
            {
                count = 0;
                out.shrink(out.size() - resbegin);
                for( int k = 0; k < val_entries.size(); k++ )
                {
                    if( val_entries[k].count > 0 && val_entries[k].var == cvar )
                    {
                        if( fixedvars.elemLim(lim>>3,k) )
                        {
                            val_entries[k].stat_flag = 1;
                        } else {
                            out.push( -1*k - 2 );
                            count++;
                        }
                    }
                }
                assert( count == 1 );
            }
        }
#endif

        for( ; i < j; i++ )
        {
            edge = IN_EDGES(kfa[i]);
            end = IN_END(kfa[i]);

            for(; edge != end; edge++ )
            {
                inc_edge& e(edges[*edge]);
                inc_node& next(nodes[e.begin]);

                if( !val_entries[e.val].stat_flag && next.stat_flag )
                {
                    assert( (next.kill_flag)&1 );

                    next.stat_flag = 0;
                    kfa.push(e.begin);
                }
            }
        }
    }
    
    i = 0;
    while( i < kfb.size() )
    {
#ifdef WEAKNOGOOD
        int resbegin = out.size();
#endif

        for( j = i; j < kfb.size(); j++ )
        {
            edge = OUT_EDGES(kfb[j]);
            end = OUT_END(kfb[j]);
            
            // Process the incoming edges
            for(; edge != end; edge++ )
            {
                assert( IS_DEAD_LIM(*edge,lim) );
                assert( !((edges[*edge].kill_flags)&1) );

                inc_edge e(edges[*edge]);
                inc_node& next(nodes[e.end]);
                
#ifdef USE_WATCHES
                int nsupp = node_edges[next.in_start];
                if( (e.kill_flags)&4 &&
                    ( !IS_DEAD_LIM(nsupp,lim) || !((next.kill_flag)&2) ) )
#else
//                if( (e.kill_flags)&4 )
                if( (e.kill_flags)&4 &&
                    (!(next.count_out == 0 && next.kill_flag < lim) || !((next.kill_flag)&2) ) )
#endif
                {
                    if( !val_entries[e.val].stat_flag )
                    {
                        if( !IS_DEAD_LIM(val_edges[val_entries[e.val].first_off],lim) )
                        {
//                            fprintf(stderr,"ARRGH: %d (%d)\n",e.val,vars[e.val].var);
                            fprintf(stderr,"ARRGH: %d\n",e.val);
                        }

                        // assert( IS_DEAD_LIM(val_entries[e.val].first_off,lim) );
                        out.push(e.val);
                        val_entries[e.val].stat_flag = 1;
                    }

                } else {
                    next.stat_flag = 1;
                }
            }
        }

#ifdef WEAKNOGOOD
        if( out.size() - resbegin >= threshold && intvars[nodes[kfb[i]].var].isFixed() )
        {
            // Check that the variable is fixed,
            // and if so rewrite the reason.
            int count = 0;
            int cvar = nodes[kfb[i]].var;
            
            // Hideously expensive.
            for( int k = 0; k < val_entries.size() ; k++ )
            {
                if( val_entries[k].count > 0 && val_entries[k].var == cvar)
                {
                    if( !fixedvars.elemLim(lim>>3,k) )
                        count++;
                }
            }

            if( count == 1 )
            {
                count = 0;
                out.shrink(out.size() - resbegin);
                for( int k = 0; k < val_entries.size(); k++ )
                {
                    if( val_entries[k].count > 0 && val_entries[k].var == cvar )
                    {
                        if( fixedvars.elemLim(lim>>3,k) )
                        {
                            val_entries[k].stat_flag = 1;
                        } else {
                            out.push( -1*k - 2 );
                            count++;
                        }
                    }
                }
                assert( count == 1 );
            }
        }
#endif
        for( ; i < j; i++ )
        {
            edge = OUT_EDGES(kfb[i]);
            end = OUT_END(kfb[i]);

            for(; edge != end; edge++ )
            {
                inc_edge e(edges[*edge]);
                inc_node& next(nodes[e.end]);

                if( !val_entries[e.val].stat_flag && next.stat_flag )
                {
                    assert( (next.kill_flag)&2 );

                    next.stat_flag = 0;
                    kfb.push(e.end);
                }
            }
        }
    }

//    for( int v = 0; v < out.size(); v++ )
    for( int v = 0; v < val_entries.size(); v++ )
    {
//        val_entries[out[v]].stat_flag = 0;
        val_entries[v].stat_flag = 0;
    }


#ifdef TIKZDEBUG
    if( run_count == 0 )
    {
        run_count++;

        fprintf(stderr,"Reason generated: ");
        for(int i = 0; i < out.size(); i++ )
        {
            fprintf(stderr, " $\\neg%d$ ",out[i]);
        }
        fprintf(stderr,"\n");
    }
#endif
}

template<int U>
void MDDProp<U>::fullConstructReason(int lim, vec<int>& out, Value val)
{
    nodes[0].stat_flag = 1;
    for( int i = 1; i < nodes.size(); i++ )
        nodes[i].stat_flag = 0;
    for( int i = 0; i < val_entries.size(); i++ )
        val_entries[i].stat_flag = 0;

    int var = (val == -1) ? -1 : val_entries[val].var;

    unsigned char ret = mark_frontier_total(var,val,lim);

    if( ret&1 )
        debugStateTikz(lim);

    assert( !(ret&1) );
    
    retrieveReason(out,var,val,lim);
//    assert(checkReason(var,val,lim));
    
    for( int i = 0; i < val_entries.size(); i++ )
        val_entries[i].stat_flag = 0;
}

template<int U>
unsigned char MDDProp<U>::mark_frontier(int node_id, int var, Value val, int lim)
{
    if( node_id == 0 )
        return 1;

    inc_node& node = nodes[node_id];

    if( node.stat_flag )
        return node.stat_flag;

    int* edge = OUT_EDGES(node_id);
    int* end = OUT_END(node_id);
    
    unsigned char ret = 2;
    
    if( node.var == var )
    {
        for(; edge != end; edge++ )
        {
            if( edges[*edge].val == val )
            {
                ret |= mark_frontier(edges[*edge].end,var,val,lim);
            } else {
                mark_frontier(edges[*edge].end,var,val,lim);
            }
        }
    } else {
        for(; edge != end; edge++ )
        {
            Value v = edges[*edge].val;
            if( fixedvars.elemLim(lim,v) )
            {
                mark_frontier(edges[*edge].end,var,val,lim);
            } else {
                ret |= mark_frontier(edges[*edge].end,var,val,lim);
            }
        }
    }

    node.stat_flag = ret;
    return ret;
}

template<int U>
unsigned char MDDProp<U>::mark_frontier_total(int var, Value val, int lim)
{
    int* edge(&(val_edges.last()));  
    int* val_start;
    
    for( int v = val_entries.size()-1; v >= 0; v-- )
    {
        if( val_entries[v].count == 0 )
            continue;

        val_start = VAL_EDGES(v);
        bool val_dead = (val != v) && ( val_entries[v].var == var || fixedvars.elemLim(lim,v) );

        if( val_dead )
        {
            for( ; edge >= val_start; edge-- )
            {
                nodes[edges[*edge].begin].stat_flag |= 2;
            }
        } else {
            for( ; edge >= val_start; edge-- )
            {
                nodes[edges[*edge].begin].stat_flag |= nodes[edges[*edge].end].stat_flag;
            }
        }
    }
    return nodes[1].stat_flag;
}

template<int U>
void MDDProp<U>::retrieveReason(vec<int>& out,int var, int val, int lim, int threshold)
{
    assert( nodes[1].var == 0 );

    kfb.clear();
    kfb.push(1); // Root node.
    
    int i = 0; // Start of the level
    int j; // Current position (kinda)
    
    int* edge;
    int* end;

    while( i < kfb.size() )
    {
        // First phase: mark things that need to be fixed.
        if( nodes[kfb[i]].var == var )
        {
            j = kfb.size();
            for(; i < j; i++ )
            {
                assert( nodes[kfb[i]].var == var );

                edge = OUT_EDGES(kfb[i]);
                end = OUT_END(kfb[i]);
                
                for(; edge != end; edge++ )
                {
                    if( edges[*edge].val != val )
                        continue;

                    kfb.push(edges[*edge].end);
                }
            }
        } else {
#ifdef WEAKNOGOOD
            int begin = out.size();
#endif
            for( j = i; j < kfb.size(); j++ )
            {
                edge = OUT_EDGES(kfb[j]);
                end = OUT_END(kfb[j]);
                
                // Process the outgoing edges
                for(; edge != end; edge++ )
                {
                    inc_edge e(edges[*edge]);
                    inc_node& next(nodes[e.end]);
    
                    if( next.stat_flag&1 )
                    {
                        if(!val_entries[e.val].stat_flag)
                        {
                            out.push(e.val);
                            val_entries[e.val].stat_flag = 1;
                        }
                    }
                }
            }

#ifdef WEAKNOGOOD
            if( out.size() - begin >= threshold )
            {
                // Check that the variable is fixed,
                // and if so rewrite the reason.
                int count = 0;
                int cvar = nodes[kfb[i]].var;
                
                // Hideously expensive.
                for( int k = 0; k < val_entries.size() ; k++ )
                {
                    if( val_entries[k].count > 0 && val_entries[k].var == cvar)
                    {
                        if( !fixedvars.elemLim(lim,k) )
                            count++;
                    }
                }

                if( count == 1 )
                {
                    count = 0;
                    out.shrink(out.size() - begin);
                    for( int k = 0; k < val_entries.size(); k++ )
                    {
                        if( val_entries[k].count > 0 && val_entries[k].var == cvar )
                        {
                            if( fixedvars.elemLim(lim,k) )
                            {
                                val_entries[k].stat_flag = 1;
                            } else {
                                out.push( -1*k - 2 );
                                count++;
                            }
                        }
                    }
                    assert( count == 1 );
                }
            }
#endif
            for( ; i < j; i++ )
            {
                edge = OUT_EDGES(kfb[i]);
                end = OUT_END(kfb[i]);
    
                for(; edge != end; edge++ )
                {
                    inc_edge e(edges[*edge]);
                    inc_node& next(nodes[e.end]);
    
                    if( !val_entries[e.val].stat_flag && next.stat_flag )
                    {
                        next.stat_flag = 0;
                        kfb.push(e.end);
                    }
                }
            }
        }
    }
}

#if 0
bool MDDProp<U>::checkReason(int var, Value val, int lim)
{
    // Assumes stat flags on vals are still set.
    int* edge(&(val_edges.last()));  
    int* val_start;
    
    for( int v = val_entries.size()-1; v >= 0; v-- )
    {
        if( val_entries[v].count == 0 )
            continue;

        val_start = VAL_EDGES(v);
        bool val_dead = (val != v) && ( val_entries[v].var == var || val_entries[v].stat_flag );

        if( val_dead )
        {
            for( ; edge >= val_start; edge-- )
            {
                nodes[edges[*edge].begin].stat_flag |= 2;
            }
        } else {
            for( ; edge >= val_start; edge-- )
            {
                nodes[edges[*edge].begin].stat_flag |= nodes[edges[*edge].end].stat_flag;
            }
        }
    }
    return !(nodes[1].stat_flag&1);
}
#endif


template<int U>
bool MDDProp<U>::fullProp(void)
{
#ifdef USE_WATCHES
    NOT_SUPPORTED;
    return false;
#else
    vec<int> inferences;
    
    // verify();
    
    for( int c = 0; c < clear_queue.size(); c++ )
    {
        assert( fixedvars.elem(clear_queue[c]) );

        val_entries[clear_queue[c]].stat_flag = 0;
        if( val_entries[clear_queue[c]].supp_count == 0 )
        {
            std::cout << prop_id << "|" << c << "|" << clear_queue[c] << std::endl;
            std::cout << clear_queue << std::endl;
        }

        assert( val_entries[clear_queue[c]].supp_count > 0 );
        trailChange(val_entries[clear_queue[c]].supp_count,0);
        inc_edge* e(edges + *(VAL_EDGES(clear_queue[c])));
        trailChange(e->kill_flags,1);
    }
    clear_queue.clear();
    
    // verify();

    nodes[0].stat_flag += 2;
    nodes[0].stat_flag |= 1;

    if( nodes[0].stat_flag&MAXTIME )
    {
        nodes[0].stat_flag = 3;

        for( int i = 1; i < nodes.size(); i++ )
        {
            nodes[i].stat_flag = 0;
        }
    }

    for( int i = 0; i < val_entries.size(); i++ )
    {
        if( fixedvars.elem(i) )
        {
            val_entries[i].stat_flag = 0;
        } else {
            val_entries[i].stat_flag = 1;
        }
    }

    unsigned char res(fullPropRec(1,nodes[0].stat_flag&~1));
    if( res&1 )
    {
        int lim = fixedvars.size();

        for( int i = 0; i < val_entries.size(); i++ )
        {
            if(val_entries[i].stat_flag == 1)
            {
                inferences.push(i);
                fixedvars.insert(i);
                val_entries[i].val_lim = lim;

                val_entries[i].stat_flag = 0;
                trailChange(val_entries[i].supp_count,0);
                inc_edge* e(edges + *(VAL_EDGES(i)));
                trailChange(e->kill_flags,1);
            }
        }
    }

    if( !(res&1) )
    {
        if( so.lazy )
        {
            // Need to assign sat.confl
            vec<int> expl;
            genReason(expl, -1);
            Clause* r = Reason_new(expl.size());
            for(int i = 0; i < expl.size(); i++ )
            {
               // Fixme: adjust to handle WEAKNOGOOD
               (*r)[i] = intvars[val_entries[expl[i]].var].getLit(val_entries[expl[i]].val,1);
            }

            sat.confl = r;
        }

        return false;
    }

    for( int i = 0; i < inferences.size(); i++ )
    {
        int v = val_entries[inferences[i]].var;
        int val = val_entries[inferences[i]].val;

        if (intvars[v].remValNotR(val))
        {
            Clause* r = NULL;
            if (so.lazy) {
               vec<int> expl;
               genReason(expl, inferences[i]);

               r = Reason_new(expl.size());

               for( int k = 1; k < expl.size(); k++ )
               {
                  (*r)[k] = intvars[val_entries[expl[k]].var].getLit(val_entries[expl[k]].val,1);
               }
            }

            if(!intvars[v].remVal(val,r)) return false;
        }
    }
    
    // verify();

    return true;
#endif
}

template<int U>
unsigned char MDDProp<U>::fullPropRec(int node, int timestamp)
{
    if( nodes[node].stat_flag >= timestamp )
        return nodes[node].stat_flag&1; 

#ifdef INSTRUMENT
        node_visits++;
        if(prop)
            (prop->infcount)++;
#endif

    int* nstart = node_edges + nodes[node].out_start;
    int* nend = nstart + nodes[node].num_out;
    
    unsigned char out = 0;
    for( ; nstart < nend; nstart++ )
    {
        if( val_entries[edges[*nstart].val].stat_flag )
        {
            if( (fullPropRec(edges[*nstart].end,timestamp)&1) )
            {
                out = 1;
                val_entries[edges[*nstart].val].stat_flag = 3;
            }
        }
    }
    nodes[node].stat_flag = timestamp|out;
    
    return out;
}

// Assumes propagator has just been run.
template<int U>
void MDDProp<U>::verify()
{
    for( int i = 0; i < val_entries.size(); i++ )
    {
        assert( intvars[val_entries[i].var].indomain(val_entries[i].val) == fixedvars.elem(i) );
        
        if( fixedvars.elem(i) )
        {
            assert( val_entries[i].val_lim <= static_cast<int>(fixedvars.pos(i)) );
        }
    }
}

template<int U>
bool MDDProp<U>::propagate()
{
#ifdef FULLPROP
    return fullProp();
#endif

    vec<int> inferences;

    kfa.clear();
    kfb.clear();
    
    vec<int> inf_temp;
        
    unsigned int count = fixedvars.size()<<3;
    
    for(int c = 0; c < clear_queue.size(); c++ )
    {
        assert( fixedvars.elem(clear_queue[c]) );

        int val = clear_queue[c];
        
        int* edge = VAL_EDGES(val);
        int* end = VAL_END(val);
        // Kill the watched edge first, to make backtracking neater.
#ifdef USE_WATCHES    
        assert( !IS_DEAD_IND(*edge) );
#endif
        for(; edge != end; edge++ )
        {
            kill_dom(count,edges + *edge,kfa,kfb);
        }
#ifndef USE_WATCHES
        if( val_entries[val].supp_count == 0 )
        {
            std::cout << prop_id << "|" << c << "|" << val << std::endl;
            std::cout << clear_queue << std::endl;
        }

        assert(VAL(val).supp_count != 0);
        trailChange(VAL(val).supp_count,0);
#endif
    }
    
    // CHECKING BOUNDS IN THE SOLVER VARS
    // verify();

    clear_queue.clear();
    
    count = fixedvars.size()<<3;
    
    int i = 0;
    while( i < kfa.size() )
    {
#ifdef INSTRUMENT
        node_visits++;
        if( prop )
            (prop->infcount)++;
#endif
        // Killed from above
        int node_id(kfa[i]);

#ifdef USE_WATCHES
        // Find new support
        int* edge(IN_EDGES(node_id));
        int* end(IN_END(node_id));

        int* watch(edge);

        assert(WATCHED_BELOW(edges + *watch));
        
        edge++;

        for( ; edge != end; edge++ )
        {
            if( !(IS_DEAD_IND(*edge)) )
            {
                int supp = *edge;
                // New support
                CLEAR_WATCH_BE(*watch);
                SET_WATCH_BE(supp);
                *edge = *watch;
                *watch = supp; 
                break;
            }
        }

        if( edge != end )
        {
            i++;
            continue;
        }
        
        nodes[node_id].kill_flag = 1;

        edge = OUT_EDGES(node_id);
        end = OUT_END(node_id);
        for( ; edge != end; edge++ )
        {
            if( IS_DEAD_IND(*edge) )
                continue;
            
            inc_edge* e(edges + *edge);
            trailChange(e->kill_flags,count|1);

            if(WATCHED_BELOW(e))
                kfa.push(e->end);
            
            if(WATCHED_VAL(e))
            {
                inf_temp.push(e->val);
            }
        }
#else
        assert( nodes[node_id].count_in == 0 );
        if( nodes[node_id].count_out == 0 )
        {
            i++;
            continue;
        }
        trailChange(nodes[node_id].count_out,0);

//        nodes[node_id].kill_flag = count|1;

        int* edge = OUT_EDGES(node_id);
        int* end = OUT_END(node_id);
        for( ; edge != end; edge++ )
        {
            if( IS_DEAD_IND(*edge) )
                continue;

            inc_edge* e(edges + *edge);
            trailChange(e->kill_flags,count|1);

            int end_node = e->end;
            
//            nodes[end_node].count_in--;
            trailChange(nodes[end_node].count_in,nodes[end_node].count_in-1);
            if(!nodes[end_node].count_in && nodes[end_node].count_out)
            {
                kfa.push(end_node);
                nodes[end_node].kill_flag = count|1;
            }
            
            Value val(e->val);
            val_entry& valent(VAL(val));
//            (valent.supp_count)--;
            trailChange(valent.supp_count,valent.supp_count-1);
            if( !(valent.supp_count) )
            {
                inferences.push(val);
                val_entries[val].val_lim = count>>3;
            }
        }
#endif

        i++;
    }

    // If the true node is dead, we have ourselves a conflict.
#ifdef USE_WATCHES
    if( IS_DEAD_IND(IN_EDGES(0)[0]) )
#else
    if( !nodes[0].count_in )
#endif
    {
        if( so.lazy )
        {
            // NOT_SUPPORTED;
            // Need to assign sat.confl
            vec<int> expl;
            genReason(expl, -1);
            Clause* r = Reason_new(expl.size());
            for(int i = 0; i < expl.size(); i++ )
            {
               // Fixme: adjust to handle WEAKNOGOOD
//               (*r)[i] = boolvars[expl[i]].getLit(0);
               (*r)[i] = intvars[val_entries[expl[i]].var].getLit(val_entries[expl[i]].val,1);
            }

            sat.confl = r;
        }

        return false;
    }

    i = 0;
    while( i < kfb.size() )
    {
#ifdef INSTRUMENT
        node_visits++;
        if( prop )
            (prop->infcount)++;
#endif

        // Killed from below
        int node_id(kfb[i]);

#ifdef USE_WATCHES
        // Find new support

        if( IS_DEAD_IND(IN_EDGES(node_id)[0]) )
        {
            i++;
            continue;
        }

        int* edge(OUT_EDGES(node_id));
        int* end(OUT_END(node_id));

        int* watch(edge);

        assert(WATCHED_ABOVE(edges + *watch));

        edge++;

        for( ; edge != end; edge++ )
        {
            if( !(IS_DEAD_IND(*edge)) )
            {
                int supp = *edge;
                // New support found
                CLEAR_WATCH_AB(*watch);
                SET_WATCH_AB(supp);
                *edge = *watch;
                *watch = supp; 
                break;
            }
        }
        
//        if( !(IS_DEAD(*watch)) )
        if( edge != end )
        {
            i++;
            continue;
        }
        
        nodes[node_id].kill_flag = 2;

        edge = IN_EDGES(node_id);
        end = IN_END(node_id);
        for( ; edge != end; edge++ )
        {
            if( IS_DEAD_IND(*edge) )
                continue;
            
            inc_edge* e(edges + *edge);
            
            trailChange(e->kill_flags,count|2);

            if(WATCHED_ABOVE(e))
                kfb.push(e->begin);

            if(WATCHED_VAL(e))
            {
                inf_temp.push(e->val);
            }
        }
#else
        assert( nodes[node_id].count_out == 0 );
        if( nodes[node_id].count_in == 0 )
        {
            i++;
            continue;
        }

        trailChange(nodes[node_id].count_in,0);
//        nodes[node_id].kill_flag = count|2;

        int* edge(IN_EDGES(node_id));
        int* nend(IN_END(node_id));

        for( ; edge != nend; edge++ )
        {
            if( IS_DEAD_IND(*edge) )
               continue;
            
            inc_edge* e(edges + *edge);
            int begin = e->begin;
            
            trailChange(e->kill_flags,count|2); 
            
            trailChange(nodes[begin].count_out,nodes[begin].count_out - 1);
            if(!nodes[begin].count_out && nodes[begin].count_in)
            {
                nodes[begin].kill_flag = count|2;
                kfb.push(begin);
            }
            
            Value val = e->val;
            val_entry& valent(VAL(val));
//            (valent.supp_count)--;
            trailChange(valent.supp_count,valent.supp_count-1);
            if( !(valent.supp_count) )
            {
                inferences.push(val);
                val_entries[val].val_lim = count>>3;
            }
        }

#endif
        i++;
    }

    count = count>>3;

#ifdef USE_WATCHES
    for(int i = 0; i < inf_temp.size(); i++ )
    {
#if 1
        Value val = inf_temp[i];
        int* edge = VAL_EDGES(val);
        int* end = VAL_END(val);

        int* watch(edge);
        
        assert(IS_DEAD_IND(*watch));

        edge++;
          
        for( ; edge != end; edge++ )
        {
             if( !(IS_DEAD_IND(*edge)) )
             {
                 int supp = *edge;
                 CLEAR_WATCH_VAL(*watch);
                 SET_WATCH_VAL(supp);
                 *edge = *watch;
                 *watch = supp;
                 break;
             }
        }
#else
        // Search-cache version.
        Value val = inf_temp[i];
        int* watch(VAL_EDGES(val));
        int* edge = VAL_CACHE(val);
        int* end = VAL_END(val);
        for(; edge != end; edge++ )
        {
             if( !(IS_DEAD_IND(*edge)) )
             {
                 int supp = *edge;
                 CLEAR_WATCH_VAL(*watch);
                 SET_WATCH_VAL(supp);
                 *edge = *watch;
                 *watch = supp;
                 val_entries[val].search_cache = edge;
                 goto found;
             }
        }

        edge = VAL_EDGES(val);
        end = VAL_CACHE(val);
        for(++edge ; edge != end; edge++ )
        {
             if( !(IS_DEAD_IND(*edge)) )
             {
                 int supp = *edge;
                 CLEAR_WATCH_VAL(*watch);
                 SET_WATCH_VAL(supp);
                 *edge = *watch;
                 *watch = supp;
                 val_entries[val].search_cache = edge;
                 break;
             }
        }
  
found:
#endif

//        if(IS_DEAD(*watch))
        if(edge == end)
        {
            inferences.push(val);
            val_entries[val].val_lim = count;

            int j;
            // Sort inferences.
            for(j = inferences.size()-1;
                    j >= 1 && val < inferences[j-1]; j-- )
            {
                inferences[j] = inferences[j-1];
            }
            inferences[j] = val;
        }
    }
#else
    for( int i = 1; i < inferences.size(); i++ )
    {
        int val = inferences[i];
        int j;
        for( j = i;
                j >= 1 && val < inferences[j-1]; j-- )
        {
            inferences[j] = inferences[j-1];
        }
        inferences[j] = val;
    }
#endif
    
    for( int i = 0; i < inferences.size(); i++ )
    {
        int v = val_entries[inferences[i]].var;
        int val = val_entries[inferences[i]].val;
        
        fixedvars.insert(inferences[i]);

//        fprintf(stderr,"~%d",var(boolvars[i].getLit(0)));  
        if (intvars[v].remValNotR(val))
        {
            Clause* r = NULL;
            if (so.lazy) {
               vec<int> expl;
               genReason(expl, inferences[i]);

               r = Reason_new(expl.size());

               for( int k = 1; k < expl.size(); k++ )
               {
//                  (*r)[k] = boolvars[expl[k]].getLit(0);
                  (*r)[k] = intvars[val_entries[expl[k]].var].getLit(val_entries[expl[k]].val,1);
               }
            }

            if(!intvars[v].remVal(val,r)) return false;
        }
    }
    
    // verify();

    return true;
}

template<int U>
void MDDProp<U>::kill_dom(unsigned int lim, inc_edge* e, vec<int>& kfa, vec<int>& kfb)
{
    if( e->kill_flags )
        return;
    
    trailChange(e->kill_flags,lim|4);
    
#ifdef USE_WATCHES     
    if(WATCHED_ABOVE(e))
        kfb.push(e->begin);
    
    if(WATCHED_BELOW(e))
        kfa.push(e->end);
#else
//    (nodes[e->begin].count_out)--;
    trailChange(nodes[e->begin].count_out,nodes[e->begin].count_out - 1);
    if( !nodes[e->begin].count_out )
    {
        nodes[e->begin].kill_flag = lim|2;
        kfb.push(e->begin);
    }

//    (nodes[e->end].count_in)--;
    trailChange(nodes[e->end].count_in,nodes[e->end].count_in - 1);
    if( !nodes[e->end].count_in )
    {
        nodes[e->end].kill_flag = lim|1;
        kfa.push(e->end);
    }
#endif
}

MDDTemplate::MDDTemplate(MDDTable& tab, MDD root, vec<int>& domain_sizes) :
        _doms(), _val_entries(), _mdd_nodes(), _val_edges(), _node_edges(), _edges()
{
    domain_sizes.copyTo(_doms);
     
    MDDCompile(tab, root, domain_sizes, _val_entries, _mdd_nodes, _edges, _val_edges, _node_edges);

#ifdef INSTRUMENT    
// #if 0
    fprintf(stderr,"%d: (Depth,Nodes,Ave Width,Aspect Ratio,Edge Count, Edge Ratio) = (%d,%d,%f,%f,%d,%f)\n",
                propnum,domain_sizes.size(),_mdd_nodes.size(),
                        1.0*_mdd_nodes.size()/domain_sizes.size(),
                        1.0*domain_sizes.size()*domain_sizes.size()/_mdd_nodes.size(),
                        _edges.size(),1.0*_edges.size()/(2*_mdd_nodes.size()));
    propnum++;
#endif
}

void MDDCompile(MDDTable& t, MDD root,
          vec<int>& domain_sizes,
          vec<val_entry>& val_entries, vec<inc_node>& inc_nodes,
          vec<inc_edge>& edge_arr,
          vec<int>& val_edges, vec<int>& node_edges)
{
    const std::vector<MDDNode>& nodes(t.getNodes());
    std::vector<int>& status(t.getStatus());

    vec<int> val_offs;

    vec< vec<int> > val_edges_sep;
    vec< vec<int> > node_in_edges;
    vec< vec<int> > node_out_edges;
    

    int valoff = 0; 
    for( int i = 0; i < domain_sizes.size(); i++ )
    {
        val_offs.push(valoff);
        for( int j = 0; j < domain_sizes[i]; j++ )
        {
            val_entry next = {
                i,   // var
                j,   // val
                0,   // Start
                0,   // Edge count
                0    // Limit
              };
            val_entries.push(next);

            val_edges_sep.push();
        }

        valoff += domain_sizes[i];
    }
    
    status[0] = 1; 
    status[1] = 1; 
    inc_nodes.push();
    inc_nodes[0].var = domain_sizes.size(); // True node.
    node_in_edges.push();
    node_out_edges.push();

    
    int count = 2; // No longer a count -- the next free id.
    // Handling a given edge.
    vec<int> node_queue;

    node_queue.push(root);
    status[root] = count;
    count++;

    // Fresh node. Initialize.
    inc_node first = {
       0, // Variable

       0, // in_start
       0, // num_in
       0, // out_start
       0 // num_out
#ifndef USE_WATCHES
       ,0 // count_in
       ,0 // count_out
#endif
       ,0 // stat_flag
       ,0 // kill_flag
       };

    inc_nodes.push(first);
    node_in_edges.push();
    node_out_edges.push();

    int qindex = 0;
    while( qindex < node_queue.size() )
    {
        int node = node_queue[qindex];
        int nodeid = status[node]-1;
        
        MDDNode nodeptr = nodes[node];
        int var = nodeptr[1]; 
        int offset = val_offs[var];
        for( unsigned int j = 2; j < nodeptr[0]; j+=2 )
        {
            if( nodeptr[j] >= ((unsigned int) domain_sizes[var]) )
                continue;

            if( status[nodeptr[j+1]] == 0 )
            {
                // Fresh node. Initialize.
                inc_node curr = {
                   nodes[nodeptr[j+1]][1], // Variable
                   0, // in_start
                   0, // num_in
                   0, // out_start
                   0 // num_out
#ifndef USE_WATCHES
                   ,0 // count_in
                   ,0 // count_out
#endif
                   };
                inc_nodes.push(curr);
                node_in_edges.push();
                node_out_edges.push();

                node_queue.push(nodeptr[j+1]);
                status[nodeptr[j+1]] = count;
                count++;
            }
            
            if( nodeptr[j+1] > 0 )
            {

                int edge_id = edge_arr.size();
                inc_edge fedge = {
                    offset+nodeptr[j], // val
                    0, // kill
                    0, // watch
                    nodeid, // begin
                    status[nodeptr[j+1]]-1 // end
                  };
                edge_arr.push(fedge);
                
                node_in_edges[status[nodeptr[j+1]]-1].push(edge_id);
                node_out_edges[nodeid].push(edge_id);

                assert( static_cast<int>(offset+nodeptr[j]) < val_edges_sep.size() );

                val_edges_sep[offset+nodeptr[j]].push(edge_id);
            }
        }

        qindex++;
    }

    for( int i = 0; i < val_edges_sep.size(); i++ )
    {
        val_entries[i].first_off = val_edges.size();                 // Start
        val_entries[i].count = val_edges_sep[i].size();   // Edge count
        
        for( int j = 0; j < val_edges_sep[i].size(); j++ )
        {
            val_edges.push(val_edges_sep[i][j]);
        }
#ifdef USE_WATCHES
        if( val_entries[i].count > 0 )
//            SET_WATCH_VAL(&edge_arr[val_edges_sep[i][0]]);
            edge_arr[val_edges_sep[i][0]].watch_flags |= 4;
#else
        val_entries[i].supp_count = val_entries[i].count; // Support count
#endif
        val_entries[i].stat_flag = 0;
        val_entries[i].search_cache = NULL;
    }
    
    assert(node_in_edges.size() == inc_nodes.size());

    for(int i = 0; i < node_in_edges.size(); i++ )
    {
        inc_node& curr = inc_nodes[i];
        curr.in_start = node_edges.size();
        curr.num_in = node_in_edges[i].size(); 

        // Sort node_in_edges
#ifdef SORTEDGES
        for( int j = 1; j < node_in_edges[i].size(); j++ )
        {
            int k;

            int temp = node_in_edges[i][j];
            int tval = edge_arr[temp].val;
            for( k = j-1; k >= 0; k-- )
            {
                if( edge_arr[node_in_edges[i][k]].val SORTOP tval )
                {
                    break;
                } else {
                    node_in_edges[i][k+1] = node_in_edges[i][k];
                }
            }
            node_in_edges[i][k+1] = temp;
        }
#endif

        for( int j = 0; j < node_in_edges[i].size(); j++ )
        {
            node_edges.push(node_in_edges[i][j]);
        }
#ifdef USE_WATCHES
        if( curr.num_in > 0 )
            edge_arr[node_in_edges[i][0]].watch_flags |= 1;
#else
        curr.count_in = curr.num_in;
#endif

//        inc_nodes[i] = curr;
    }

    for(int i = 0; i < node_out_edges.size(); i++ )
    {
        inc_node& curr = inc_nodes[i];
        curr.out_start = node_edges.size();
        curr.num_out = node_out_edges[i].size();

        // Sort node_out_edges
#ifdef SORTEDGES
        for( int j = 1; j < node_out_edges[i].size(); j++ )
        {
            int k;

            int temp = node_out_edges[i][j];
            int tval = edge_arr[temp].val;
            for( k = j-1; k >= 0; k-- )
            {
                if( edge_arr[node_out_edges[i][k]].val SORTOP tval )
                {
                    break;
                } else {
                    node_out_edges[i][k+1] = node_out_edges[i][k];
                }
            }
            node_out_edges[i][k+1] = temp;
        }
#endif
        
        for( int j = 0; j < node_out_edges[i].size(); j++ )
        {
            node_edges.push(node_out_edges[i][j]);
        }

#ifdef USE_WATCHES
        if( curr.num_out > 0 )
            edge_arr[node_out_edges[i][0]].watch_flags |= 2;
#else
        curr.count_out = curr.num_out;
#endif
    }

    for(int i = 0; i < node_queue.size(); i++ )
    {
        status[node_queue[i]] = 0;
    }
}

// Hack to get the template to compile.
template class MDDProp<0>;
