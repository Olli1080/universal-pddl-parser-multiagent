
#pragma once

#include <parser/Domain.h>

#include <multiagent/ConcurrentAction.h>
#include <multiagent/ConcurrencyPredicate.h>
#include <multiagent/ConcurrencyGround.h>

namespace parser { namespace multiagent {

using pddl::Filereader;

class ConcurrencyDomain : public pddl::Domain
{
public:
	using Base = pddl::Domain;

	bool multiagent, unfact, fact;	// whether domain is multiagent, unfactored or factored

	TokenStruct<std::shared_ptr<ConcurrencyPredicate>> cpreds;	// concurrency predicates

	std::set<std::shared_ptr<ConcurrencyGround>> pendingConcurrencyGrounds;

	ConcurrencyDomain()
		: Base(), multiagent( false ), unfact( false ), fact( false ) {}

	ConcurrencyDomain( const std::string& s )
		: Base(), multiagent( false ), unfact( false ), fact( false )
	{
		parse(s);
	}

	virtual ~ConcurrencyDomain() override = default;
		// cpreds are also contained in preds, so do not delete them
		// (they'll be deleted in the base class)

	bool parseBlock(const std::string& t, Filereader& f) override
	{
		if (Base::parseBlock(t, f)) return true;

		return false;
	}

	bool parseRequirement( const std::string& s ) override
	{
		if (Base::parseRequirement(s)) return true;

		// Parse possible requirements of a multi-agent domain
		if ( s == "MULTI-AGENT" ) multiagent = true;
		else if ( s == "UNFACTORED-PRIVACY" ) unfact = true;
		else if ( s == "FACTORED-PRIVACY" ) fact = true;
		else return false;

		return true;
	}

	void parseAction( Filereader & f ) override
	{
		if (preds.empty()) 
		{
			std::cout << "Predicates needed before defining actions\n";
			exit(1);
		}

		f.next();
		std::shared_ptr<pddl::Action> a;

		// If domain is multiagent, parse using AgentAction
		if ( multiagent ) a = std::make_shared<ConcurrentAction>( f.getToken() );
		else a = std::make_shared<pddl::Action>( f.getToken() );

		a->parse( f, types[0]->constants, *this );

		if constexpr ( DOMAIN_DEBUG ) std::cout << a << "\n";
		actions.insert( a );

		// create a predicate that corresponds to the action being parsed and add it
		// to the domain
		addConcurrencyPredicateFromAction(*a);
	}

	void addConcurrencyPredicateFromAction(const pddl::Action& a )
	{
		auto cp = std::make_shared<ConcurrencyPredicate>( a.name );
		cp->params = IntVec( a.params );
		preds.insert( cp );
		cpreds.insert( cp );

		//
		for (const auto& pendingConcurrencyGround : pendingConcurrencyGrounds)
		{
			std::string groundName = pendingConcurrencyGround->name;
			if ( groundName == a.name ) {
				pendingConcurrencyGround->setLifted( cp, *this );
			}
		}
	}

	std::ostream& print_requirements(std::ostream& os) const override
	{
		os << "( :REQUIREMENTS";
		if ( equality ) os << " :EQUALITY";
		if ( strips ) os << " :STRIPS";
		if ( costs ) os << " :ACTION-COSTS";
		if ( adl ) os << " :ADL";
		if ( neg ) os << " :NEGATIVE-PRECONDITIONS";
		if ( condeffects ) os << " :CONDITIONAL-EFFECTS";
		if ( typed ) os << " :TYPING";
		if ( temp ) os << " :DURATIVE-ACTIONS";
		if ( nondet ) os << " :NON-DETERMINISTIC";
		if ( multiagent ) os << " :MULTI-AGENT";
		if ( unfact ) os << " :UNFACTORED-PRIVACY";
		if ( fact ) os << " :FACTORED-PRIVACY";
		os << " )\n";
		return os;
	}

	std::ostream& print(std::ostream& os) const override
	{
		os << "( DEFINE ( DOMAIN " << name << " )\n";
		print_requirements(os);

		if ( typed ) 
		{
			os << "( :TYPES\n";
			for ( unsigned i = 1; i < types.size(); ++i )
				types[i]->PDDLPrint( os );
			os << ")\n";
		}

		if ( cons ) 
		{
			os << "( :CONSTANTS\n";
			for (const auto& type : types)
			{
				if (!type->constants.empty())
				{
					os << "\t";
					for (const auto& constant : type->constants)
						os << constant << " ";
					if (typed)
						os << "- " << type->name;
					os << "\n";
				}
			}
			os << ")\n";
		}

		printPredicates( os );

		if (!funcs.empty()) 
		{
			os << "( :FUNCTIONS\n";
			for (const auto& func : funcs)
			{
				func->PDDLPrint(os, 1, TokenStruct<std::string>(), *this);
				os << "\n";
			}
			os << ")\n";
		}

		for (const auto& action : actions)
			action->PDDLPrint(os, 0, TokenStruct<std::string>(), *this);

		for (const auto& i : derived)
			i->PDDLPrint(os, 0, TokenStruct<std::string>(), *this);

		print_addtional_blocks(os);

		os << ")\n";

		return os;
	}

	std::ostream& printPredicates( std::ostream& os ) const
	{
		os << "( :PREDICATES\n";
		for (const auto& pred : preds)
		{
			if ( cpreds.index(pred->name ) == -1 )
			{
				pred->PDDLPrint(os, 1, TokenStruct<std::string>(), *this);
				os << "\n";
			}
		}
		os << ")\n";

		return os;
	}

	std::shared_ptr<pddl::Condition> createCondition( Filereader & f ) override
	{
		std::string s = f.getToken();

		if ( s == "=" ) return std::make_shared<pddl::Equals>();
		if ( s == "AND" ) return std::make_shared<pddl::And>();
		if ( s == "EXISTS" ) return std::make_shared<pddl::Exists>();
		if ( s == "FORALL" ) return std::make_shared<pddl::Forall>();
		if ( s == "INCREASE" ) return std::make_shared<pddl::Increase>();
		if ( s == "NOT" ) return std::make_shared<pddl::Not>();
		if ( s == "ONEOF" ) return std::make_shared<pddl::Oneof>();
		if ( s == "OR" ) return std::make_shared<pddl::Or>();
		if ( s == "WHEN" ) return std::make_shared<pddl::When>();

		int i = preds.index( s );
		if ( i >= 0 ) 
		{
			return std::make_shared<pddl::Ground>( preds[i] );
		}
		else 
		{
			// they are saved to be assigned later a Lifted predicate
			// each time an action is parsed
			auto cg = std::make_shared<ConcurrencyGround>(s);
			pendingConcurrencyGrounds.insert( cg );
			return cg;
		}

		f.tokenExit( s );

		return nullptr;
	}
};

} } // namespaces
