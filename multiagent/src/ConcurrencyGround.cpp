#include <parser/Domain.h>

#include <multiagent/ConcurrencyGround.h>

namespace parser { namespace multiagent {

void ConcurrencyGround::parse( Filereader & f, TokenStruct< std::string > & ts, pddl::Domain & d )
{
	f.next();

	std::string lastToken = f.getToken();
	while (!lastToken.empty()) 
	{
		int k = ts.index( lastToken );
		if ( k >= 0 ) params.emplace_back(k);
		else {
			constants[params.size()] = lastToken;
			params.push_back( -1 );
		}

		f.next();
		lastToken = f.getToken();
	}

	f.assert_token( ")" );
}

void ConcurrencyGround::setLifted(const std::shared_ptr<pddl::Lifted>& l, pddl::Domain & d )
{
	lifted = l;
	auto lock = lifted.lock();
	for (auto& constant : constants)
	{
		std::pair< bool, int > p = d.types[lock->params[constant.first]]->parseConstant(constant.second );
		if ( p.first ) params[constant.first] = p.second;
		else {
			std::cout << "error" << std::endl;
		}
	}
}

} } // namespaces
