
#pragma once

#include <parser/Lifted.h>

namespace parser { namespace multiagent {

using pddl::TokenStruct;
using pddl::Filereader;

class ConcurrencyPredicate : public pddl::Lifted
{
public:

	ConcurrencyPredicate() = default;

	ConcurrencyPredicate( const std::string & s )
		: pddl::Lifted( s ) {}

	ConcurrencyPredicate(const ParamCond& c)
		: pddl::Lifted( c ) {}

	/*
	void PDDLPrint( std::ostream & s, unsigned indent, const TokenStruct< std::string > & ts, const pddl::Domain & d ) const override;
	
	void parse( Filereader & f, TokenStruct< std::string > & ts, pddl::Domain & d );
	*/

	[[nodiscard]] std::shared_ptr<Condition> copy(const pddl::Domain& d) const override
	{
		return std::make_shared<ConcurrencyPredicate>(*this);
	}
};

} } // namespaces
