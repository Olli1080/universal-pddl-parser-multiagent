
#include <multiagent/MultiagentDomain.h>
#include <multiagent/NetworkNode.h>

namespace parser { namespace multiagent {

using pddl::Lifted;

void NetworkNode::PDDLPrint( std::ostream & s, unsigned indent, const TokenStruct< std::string > & ts, const Domain & d ) const
{
	s << "( :CONCURRENCY-CONSTRAINT " << name << "\n";

	s << "  :PARAMETERS ";

	TokenStruct< std::string > nstruct;

	printParams( 0, s, nstruct, d );

	s << "  :BOUNDS ( " << lower << " ";
	if ( upper < 1000000 ) s << upper;
	else s << "INF";
	s << " )\n";

	s << "  :ACTIONS (";
	for (const auto& i : templates)
	{
		s << " ( " << i->name;
		for (int param : i->params)
			s << " " << param;
		s << " )";
	}
	s << " )\n";
	s << ")\n";
}

void NetworkNode::parse( Filereader & f, TokenStruct< std::string > & ts, Domain & d )
{
	f.next();
	f.assert_token( ":PARAMETERS" );
	f.assert_token( "(" );
	TokenStruct< std::string > nstruct = f.parseTypedList( true, d.types );
	params = d.convertTypes( nstruct.types );
		
	f.next();
	f.assert_token( ":BOUNDS" );
	f.assert_token( "(" );
	std::string lo = f.getToken();
	std::istringstream( lo ) >> lower;
	f.next();
	std::string hi = f.getToken();
	if ( hi == "INF" ) upper = 1000000;
	else std::istringstream( hi ) >> upper;
	f.next();
	f.assert_token( ")" );

	f.assert_token( ":ACTIONS" );
	f.assert_token( "(" );
	while ( f.getChar() != ')' ) 
	{
		f.assert_token( "(" );
		int action = d.actions.index( f.getToken( d.actions ) );
		f.next();

		auto c = std::make_shared<Lifted>( d.actions[action]->name );
		c->params.resize( params.size() );
		for ( unsigned i = 0; i < params.size(); ++i ) {
			std::string index = f.getToken();
			std::istringstream( index ) >> c->params[i];
			f.next();
		}
		templates.emplace_back( c );
		f.assert_token( ")" );
	}

	++f.c;
	f.next();
	f.assert_token( ")" );
}

} } // namespaces
