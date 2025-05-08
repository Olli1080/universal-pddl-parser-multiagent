// To check for memory leaks:
// valgrind --leak-check=yes examples/serialize ../multiagent/codmap/domains/tablemover/tablemover.pddl ../multiagent/codmap/domains/tablemover/table1_1.pddl

#include <parser/Instance.h>
#include <multiagent/MultiagentDomain.h>

using namespace parser::pddl;

std::unique_ptr<parser::multiagent::MultiagentDomain> d;
std::unique_ptr<Instance> ins;
std::set<unsigned> prob;

typedef std::map< unsigned, std::vector< int > > VecMap;

static bool deletes( const Ground& ground, const parser::multiagent::NetworkNode& n, unsigned k)
{
	for ( unsigned i = 0; i < n.templates.size(); ++i )
		if ( i != k ) {
			auto a = d->actions[d->actions.index( n.templates[i]->name )];
			auto pres = a->precons();
			for (const auto& pre : pres)
			{
				auto g = std::dynamic_pointer_cast<Ground>(pre);
				if ( g && g->name == ground.name &&
				     std::ranges::find(g->params, 0 ) == g->params.end() )
					return true;
			}
		}
	return false;
}

// returns true if at least one instance of "POS-" or "NEG-" added
bool addEff( Domain& cd, Action& a, const std::shared_ptr<Condition>& c )
{
	auto n = std::dynamic_pointer_cast<Not>(c);
	auto g = std::dynamic_pointer_cast<Ground>(c);
	if ( n && prob.contains( d->preds.index( n->cond->name ) )) {
		cd.addEff( false, a.name, "NEG-" + n->cond->name, n->cond->params );
		return true;
	}
	if ( g && prob.contains( d->preds.index( g->name ) )) {
		cd.addEff( false, a.name, "POS-" + g->name, g->params );
		return true;
	}
	
	if ( n )
		cd.addEff( true, a.name, n->cond->name, n->cond->params );
	else if ( g )
		cd.addEff( false, a.name, g->name, g->params );
	else if ( c ) 
	{
		if ( !a.eff ) a.eff = std::make_shared<And>();
		const auto aa = std::dynamic_pointer_cast<And>(a.eff);
		aa->add( c->copy( cd ) );
	}

	return false;
}

int main( int argc, char *argv[] )
{
	if ( argc < 3 ) 
	{
		std::cout << "Usage: ./transform <domain.pddl> <task.pddl>\n";
		exit( 1 );
	}

	// Read multiagent domain and instance with associated concurrency network

	d = std::make_unique<parser::multiagent::MultiagentDomain>(argv[1]);
	ins = std::make_unique<Instance>(*d, argv[2]);

	// Identify problematic fluents (preconditions deleted by agents)
	// For now, disregard edges

	for ( unsigned i = 0; i < d->nodes.size(); ++i ) 
	{
		for ( unsigned j = 0; d->nodes[i]->upper > 1 && j < d->nodes[i]->templates.size(); ++j ) 
		{
			auto a = d->actions[d->actions.index( d->nodes[i]->templates[j]->name )];
			const auto dels = a->deleteEffects();

			for (auto& del : dels)
			{
				if ( std::ranges::find(del->params, 0 ) == del->params.end() &&
				     deletes(*del, *d->nodes[i], j ) ) {
					prob.insert( d->preds.index(del->name ) );
				}
			}
		}
	}

	VecMap ccs;
	for ( unsigned i = 0; i < d->mf.size(); ++i )
		ccs[d->mf[i]].push_back( i );

	// Create classical domain
	auto cd = std::make_shared<Domain>();
	cd->name = d->name;
	cd->condeffects = cd->cons = cd->typed = true;

	// Add types
	cd->setTypes( d->copyTypes() );
	cd->createType( "AGENT-COUNT" );

	// Add constants
	cd->createConstant( "ACOUNT-0", "AGENT-COUNT" );

	// Add predicates
	for ( unsigned i = 0; i < d->preds.size(); ++i ) 
	{
		cd->createPredicate(d->preds[i]->name, d->typeList(*d->preds[i]));
		if ( prob.contains( i )) 
		{
			cd->createPredicate( "POS-" + d->preds[i]->name );
			cd->createPredicate( "NEG-" + d->preds[i]->name );
		}
	}
	cd->createPredicate( "AFREE" );
	cd->createPredicate( "ATEMP" );
	cd->createPredicate( "TAKEN", StringVec( 1, "AGENT" ) );
	cd->createPredicate( "CONSEC", StringVec( 2, "AGENT-COUNT" ) );
	for ( unsigned i = 0; i < d->nodes.size(); ++i ) 
	{
		auto j = ccs.find( d->mf[i] );
		if ( j->second.size() > 1 || d->nodes[i]->upper > 1 ) {
			cd->createPredicate( "ACTIVE-" + d->nodes[i]->name, d->typeList(*d->nodes[i]));
			cd->createPredicate( "COUNT-" + d->nodes[i]->name, StringVec( 1, "AGENT-COUNT" ) );
			cd->createPredicate( "SAT-" + d->nodes[i]->name, StringVec( 1, "AGENT-COUNT" ) );
		}
		if ( j->second.size() > 1 ) 
		{
			cd->createPredicate( "USED-" + d->nodes[i]->name );
			cd->createPredicate( "DONE-" + d->nodes[i]->name );
			cd->createPredicate( "SKIPPED-" + d->nodes[i]->name );
		}
	}

	// Add actions
	for (auto& cc : ccs)
	{
		std::set< unsigned > visited;
		for ( unsigned j = 0; j < cc.second.size(); ++j ) 
		{
			int x = cc.second[j];
			visited.insert( x );

			if (cc.second.size() > 1 || d->nodes[x]->upper > 1 ) {
				std::string name = "START-" + d->nodes[x]->name;
				unsigned size = d->nodes[x]->params.size();
				cd->createAction( name, d->typeList( *d->nodes[x] ) );

				if ( j > 0 )
				{
					for ( unsigned k = 0; k < d->edges.size(); ++k )
						if ( d->edges[k].second == x ) 
						{
							auto it = visited.find( d->edges[k].first );
							if ( it != visited.end() )
								cd->addPre( false, name, "DONE-" + d->nodes[d->edges[k].first]->name );
						}
					cd->addOrPre( name, "DONE-" + d->nodes[cc.second[j - 1]]->name, "SKIPPED-" + d->nodes[cc.second[j - 1]]->name );
					cd->addPre( false, name, "ACTIVE-" + d->nodes[cc.second[j - 1]]->name, incvec( 0, size ) );
					cd->addPre( true, name, "USED-" + d->nodes[x]->name );
					
				}
				else cd->addPre( false, name, "AFREE" );

				if ( j < 1 ) cd->addEff( true, name, "AFREE" );
				cd->addEff( false, name, "ACTIVE-" + d->nodes[x]->name, incvec( 0, size ) );
				cd->addEff( false, name, "COUNT-" + d->nodes[x]->name, IntVec( 1, -1 ) );
				if (cc.second.size() > 1 )
					cd->addEff( false, name, "USED-" + d->nodes[x]->name );
			}

			if (cc.second.size() > 1 ) {
				std::string name = "SKIP-" + d->nodes[x]->name;
				unsigned size = d->nodes[x]->params.size();
				cd->createAction( name, d->typeList(*d->nodes[x]));

				if ( j > 0 ) {
					for ( unsigned k = 0; k < d->edges.size(); ++k )
						if ( d->edges[k].first == cc.second[j] ) {
							auto it = visited.find( d->edges[k].second );
							if ( it != visited.end() )
								cd->addPre( false, name, "SKIPPED-" + d->nodes[d->edges[k].second]->name );
						}
					cd->addOrPre( name, "DONE-" + d->nodes[cc.second[j - 1]]->name, "SKIPPED-" + d->nodes[cc.second[j - 1]]->name );
					cd->addPre( false, name, "ACTIVE-" + d->nodes[cc.second[j - 1]]->name, incvec( 0, size ) );
					cd->addPre( true, name, "USED-" + d->nodes[x]->name );
				}
				else cd->addPre( false, name, "AFREE" );

				if ( !j ) cd->addEff( true, name, "AFREE" );
				cd->addEff( false, name, "ACTIVE-" + d->nodes[x]->name, incvec( 0, size ) );
				cd->addEff( false, name, "SKIPPED-" + d->nodes[x]->name );
				cd->addEff( false, name, "USED-" + d->nodes[x]->name );
			}

			bool concurEffs = false;
			for ( unsigned k = 0; k < d->nodes[x]->templates.size(); ++k ) 
			{
				int action = d->actions.index( d->nodes[x]->templates[k]->name );
				std::string name = "DO-" + d->actions[action]->name;
				unsigned size = d->actions[action]->params.size();
				auto doit = cd->createAction( name, d->typeList(*d->actions[action]));

				// copy old preconditions
				auto oldpre = std::dynamic_pointer_cast<And>( d->actions[action]->pre );
				if ( oldpre ) doit->pre = std::make_shared<And>(*oldpre, *cd );
				else {
					auto a = std::make_shared<And>();
					a->add( d->actions[action]->pre->copy( *cd ) );
					doit->pre = a;
				}

				// copy old effects
				auto oldeff = std::dynamic_pointer_cast<And>( d->actions[action]->eff );
				for ( unsigned l = 0; oldeff && l < oldeff->conds.size(); ++l )
					concurEffs |= addEff( *cd, *doit, oldeff->conds[l] );
				if ( !oldeff ) concurEffs |= addEff( *cd, *doit, d->actions[action]->eff );

				// add new parameters
				if (cc.second.size() > 1 || d->nodes[x]->upper > 1 )
					cd->addParams( name, StringVec( 2, "AGENT-COUNT" ) );

				// add new preconditions
				if (cc.second.size() > 1 || d->nodes[x]->upper > 1 ) {
					cd->addPre( false, name, "ACTIVE-" + d->nodes[x]->name, d->nodes[x]->templates[k]->params );
					cd->addPre( true, name, "TAKEN", IntVec( 1, 0 ) );
					cd->addPre( false, name, "COUNT-" + d->nodes[x]->name, incvec( size, size + 1 ) );
					cd->addPre( false, name, "CONSEC", incvec( size, size + 2 ) );
				}
				else cd->addPre( false, name, "AFREE" );

				// add new effects
				if (cc.second.size() > 1 || d->nodes[x]->upper > 1 ) {
					cd->addEff( false, name, "TAKEN", IntVec( 1, 0 ) );
					cd->addEff( true, name, "COUNT-" + d->nodes[x]->name, incvec( size, size + 1 ) );
					cd->addEff( false, name, "COUNT-" + d->nodes[x]->name, incvec( size + 1, size + 2 ) );
				}
			}

			if (cc.second.size() > 1 || d->nodes[x]->upper > 1) 
			{
				std::string name = "END-" + d->nodes[x]->name;
				unsigned size = d->nodes[x]->params.size();
				auto end = cd->createAction( name, d->typeList(*d->nodes[x]));
				cd->addParams( name, StringVec( 1, "AGENT-COUNT" ) );

				cd->addPre( false, name, "COUNT-" + d->nodes[x]->name, incvec( size, size + 1 ) );
				cd->addPre( false, name, "SAT-" + d->nodes[x]->name, incvec( size, size + 1 ) );
				cd->addPre( false, name, "ACTIVE-" + d->nodes[x]->name, incvec( 0, size ) );

				cd->addEff( true, name, "COUNT-" + d->nodes[x]->name, incvec( size, size + 1 ) );
				if (cc.second.size() > 1 )
					cd->addEff( false, name, "DONE-" + d->nodes[x]->name );
				else {
					cd->addEff( false, name, concurEffs ? "ATEMP" : "AFREE" );
					cd->addEff( true, name, "ACTIVE-" + d->nodes[x]->name, incvec( 0, size ) );
					auto f = std::make_shared<Forall>();
					f->params = cd->convertTypes( StringVec( 1, "AGENT" ) );
					f->cond = std::make_shared<Not>(std::make_shared<Ground>( cd->preds.get( "TAKEN" ), incvec( size + 1, size + 2 ) ) );
					std::dynamic_pointer_cast<And>( end->eff )->add( f );
				}
			}

			if (cc.second.size() > 1 && j + 1 == cc.second.size() ) 
			{
				std::string name = "FINISH-" + d->nodes[x]->name;
				unsigned size = d->nodes[x]->params.size();
				auto finish = cd->createAction(name, d->typeList(*d->nodes[x]));

				cd->addOrPre( name, "DONE-" + d->nodes[x]->name, "SKIPPED-" + d->nodes[x]->name );
				cd->addPre( false, name, "ACTIVE-" + d->nodes[x]->name, incvec( 0, size ) );

				cd->addEff( false, name, "ATEMP" );
				for (int k : cc.second)
				{
					cd->addEff( true, name, "DONE-" + d->nodes[k]->name );
					cd->addEff( true, name, "SKIPPED-" + d->nodes[k]->name );
					cd->addEff( true, name, "USED-" + d->nodes[k]->name );
					cd->addEff( true, name, "ACTIVE-" + d->nodes[k]->name, incvec( 0, size ) );
				}
				auto f = std::make_shared<Forall>();
				f->params = cd->convertTypes( StringVec( 1, "AGENT" ) );
				f->cond = std::make_shared<Not>(std::make_shared<Ground>(cd->preds.get( "TAKEN" ), incvec( size, size + 1 )));
				std::dynamic_pointer_cast<And>(finish->eff)->add(f);
			}
		}
	}

	for (unsigned int i : prob)
	{
		std::string name = "ADD-" + d->preds[i]->name;
		size_t size = d->preds[i]->params.size();
		cd->createAction( name, d->typeList( *d->preds[i] ) );
		cd->addPre( false, name, "ATEMP" );
		cd->addPre( false, name, "POS-" + d->preds[i]->name, incvec( 0, size ) );
		cd->addPre( true, name, "NEG-" + d->preds[i]->name, incvec( 0, size ) );
		cd->addEff( false, name, d->preds[i]->name, incvec( 0, size ) );
		cd->addEff( true, name, "POS-" + d->preds[i]->name, incvec( 0, size ) );

		name = "DELETE-" + d->preds[i]->name;
		cd->createAction( name, d->typeList( *d->preds[i] ) );
		cd->addPre( false, name, "ATEMP" );
		cd->addPre( true, name, "POS-" + d->preds[i]->name, incvec( 0, size ) );
		cd->addPre( false, name, "NEG-" + d->preds[i]->name, incvec( 0, size ) );
		cd->addEff( true, name, d->preds[i]->name, incvec( 0, size ) );
		cd->addEff( true, name, "NEG-" + d->preds[i]->name, incvec( 0, size ) );
	}

	auto freeit = cd->createAction( "FREE" );
	cd->addPre( false, "FREE", "ATEMP" );
	for (unsigned int i : prob)
	{
		auto f = std::make_shared<Forall>();
		f->params = cd->convertTypes( d->typeList( *d->preds[i] ) );
		auto a = std::make_shared<And>();
		a->add(std::make_shared<Not>(std::make_shared<Ground>( cd->preds.get( "POS-" + d->preds[i]->name ), incvec( 0, f->params.size() ) ) ) );
		a->add(std::make_shared<Not>(std::make_shared<Ground>( cd->preds.get( "NEG-" + d->preds[i]->name ), incvec( 0, f->params.size() ) ) ) );
		f->cond = a;
		std::dynamic_pointer_cast<And>(freeit->pre)->add(f);
	}
	cd->addEff( false, "FREE", "AFREE" );
	cd->addEff( true, "FREE", "ATEMP" );

	std::cout << *cd;

	// Generate single-agent instance

	size_t nagents = d->types.get( "AGENT" )->noObjects();

	auto cins = std::make_shared<Instance>(*cd);
	cins->name = ins->name;

	// add objects
	StringVec counts( 1, "ACOUNT-0" );
	for ( unsigned i = 1; i <= nagents; ++i ) {
		std::stringstream ss;
		ss << "ACOUNT-" << i;
		counts.push_back( ss.str() );
		cins->addObject( counts[i], "AGENT-COUNT" );
	}

	// create initial state
	for (auto& i : ins->init)
		if ( d->preds.index(i->name ) >= 0 )
			cins->addInit(i->name, d->objectList(*i) );
	cins->addInit( "AFREE" );
	for ( unsigned i = 1; i <= nagents; ++i ) {
		StringVec pars( 1, counts[i - 1] );
		pars.push_back( counts[i] );
		cins->addInit( "CONSEC", pars );
	}
	for ( unsigned i = 0; i < d->nodes.size(); ++i ) {
		auto j = ccs.find( d->mf[i] );
		if ( j->second.size() > 1 || d->nodes[i]->upper > 1 ) {
			for ( unsigned j = d->nodes[i]->lower; j <= d->nodes[i]->upper && j <= nagents; ++j )
				cins->addInit( "SAT-" + d->nodes[i]->name, StringVec( 1, counts[j] ) );
		}
	}

	// create goal state
	for (auto& i : ins->goal)
		cins->addGoal(i->name, d->objectList(*i) );
	cins->addGoal( "AFREE" );

	std::cerr << *cins;

	//delete cins;
	//delete cd;
	//delete ins;
	//delete d;
}