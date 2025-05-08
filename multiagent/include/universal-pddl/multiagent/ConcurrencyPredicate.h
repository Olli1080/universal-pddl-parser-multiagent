
#pragma once

#include <parser/Lifted.h>

namespace parser { namespace multiagent {

using pddl::TokenStruct;
using pddl::Filereader;

class ConcurrencyPredicate : public pddl::Lifted
{
public:

	ConcurrencyPredicate() {}

	ConcurrencyPredicate( const std::string & s )
		: pddl::Lifted( s ) {}

	ConcurrencyPredicate(const ParamCond& c)
		: pddl::Lifted( c ) {}

	/*
	void PDDLPrint( std::ostream & s, unsigned indent, const TokenStruct< std::string > & ts, const pddl::Domain & d ) const override;
	
	void parse( Filereader & f, TokenStruct< std::string > & ts, pddl::Domain & d );
	*/

	std::shared_ptr<Condition> copy(pddl::Domain& d) override
	{
		return std::make_shared<ConcurrencyPredicate>(*this);
	}
};

} } // namespaces
