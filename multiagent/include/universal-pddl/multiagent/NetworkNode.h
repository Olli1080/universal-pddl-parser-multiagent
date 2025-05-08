
#pragma once

#include <parser/ParamCond.h>

namespace parser { namespace multiagent {

using pddl::Filereader;
using pddl::Domain;

class NetworkNode : public pddl::ParamCond {
public:
	unsigned lower, upper;
	std::vector<std::shared_ptr<pddl::ParamCond>> templates;
  
	NetworkNode( const std::string & s )
		: pddl::ParamCond( s ), lower( 0 ), upper( 0 ) {}

	NetworkNode( const NetworkNode& n, Domain & d )
		: pddl::ParamCond( n ), lower( n.lower ), upper( n.upper ) {
		for (const auto& i : n.templates)
			templates.emplace_back(std::dynamic_pointer_cast<pddl::ParamCond>(i->copy(d)));
	}

	~NetworkNode() override = default;

	void print( std::ostream & stream ) const override
	{
		stream << "Network node ";
		pddl::ParamCond::print( stream );
		stream << "  <" << lower << "," << upper << ">";
		for (const auto& i : templates)
			stream << "\n  Template: " << i;
	}

	void PDDLPrint( std::ostream & s, unsigned indent, const pddl::TokenStruct< std::string > & ts, const Domain & d ) const override;

	void parse( pddl::Filereader & f, pddl::TokenStruct< std::string > & ts, Domain & d ) override;

	void addParams( int m, unsigned n ) override
	{
	}

	std::shared_ptr<Condition> copy(Domain& d) override
	{
		return std::make_shared<NetworkNode>(*this, d);
	}
	
};

} } // namespaces
