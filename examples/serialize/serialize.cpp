
#include <parser/Instance.h>
#include <multiagent/ConcurrencyDomain.h>
#include <algorithm>
#include <typeinfo>
#include <fstream>
#include <cstring>

using namespace parser::pddl;

void showHelp() {
    std::cout << "Usage: ./serialize [options] <domain.pddl> <task.pddl>\n";
    std::cout << "Options:\n";
    std::cout << "    -h                             -- Print this message.\n";
    std::cout << "    -j, --max-joint-action-size    -- Maximum number of atomic actions per joint action.\n";
    std::cout << "    -o, --use-agent-order          -- Agents do actions in an specific order.\n";
    exit( 1 );
}

typedef struct ProgramParams {
    std::string domain, ins;
    bool agentOrder; // use fixed agent order
    int maxJointActionSize; // maximum number of atomic actions per joint action
    bool help;

    ProgramParams( int argc, char * argv[] ) : agentOrder( false ), maxJointActionSize( -1 ), help( false ) {
        parseInputParameters( argc, argv );
    }

    void parseInputParameters( int argc, char * argv[] ) {
        if ( argc < 3 ) {
            showHelp();
        }

        int domainParam = 0;
        for ( int i = 1; i < argc; ++i ) {
            if ( argv[i][0] == '-' ) {
                if ( !strcmp( argv[i], "-j" ) || !strcmp( argv[i], "--max-joint-action-size" ) ) {
                    if ( i + 1 < argc ) {
                        maxJointActionSize = atoi( argv[i + 1] );
                        ++i;
                    }
                    else {
                        showHelp();
                    }
                }
                else if ( !strcmp( argv[i], "-o" ) || !strcmp( argv[i], "--use-agent-order" ) ) {
                    agentOrder = true;
                }
                else if ( !strcmp( argv[i], "-h" ) ) {
                    help = true;
                }
            }
            else {
                domainParam = i;
                break;
            }
        }

        if ( argc > domainParam + 1 ) {
            domain = argv[domainParam];
            ins = argv[domainParam + 1];
        }
        else {
            showHelp();
        }
    }

} ProgramParams;

void addTypes(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, bool useAgentOrder, int maxJointActionSize )
{
    cd.setTypes(d.copyTypes());

    if (useAgentOrder)
        cd.createType("AGENT-ORDER-COUNT");

    if (maxJointActionSize > 0)
        cd.createType("ATOMIC-ACTION-COUNT");
}

void addAgentType(parser::multiagent::ConcurrencyDomain& d)
{
    // in some domains, the AGENT type is not specified, so we add the type
    // manually
    // all the types in :agent have their supertype set to AGENT (if they do not
    // already have it)
    if ( d.types.index( "AGENT" ) < 0 ) 
    {
        // get types of agents (first parameter of actions)
        std::set<std::shared_ptr<Type>> agentTypes;
        for ( unsigned i = 0; i < d.actions.size(); ++i ) 
        {
            auto action = d.actions[i];
            StringVec actionParams = d.typeList(*action);
            if (!actionParams.empty()) 
            {
                const std::string& firstParamStr = actionParams[0];
                auto firstParamType = d.getType( firstParamStr );
                agentTypes.insert( firstParamType );
            }
        }

        // get supertypes only (as subtypes are already covered by supertypes)
        std::set<std::shared_ptr<Type>> agentSupertypes;
        for ( auto it = agentTypes.begin(); it != agentTypes.end(); ++it ) 
        {
            auto currentType = *it;
            auto itType = currentType;
            bool isSupertype = true;
            while ( itType ) 
            {
                auto parentType = itType->supertype;
                bool inAgentSet = agentTypes.contains(parentType.lock());
                if ( inAgentSet ) 
                {
                    isSupertype = false;
                    break;
                }
                itType = parentType.lock();
            }
            if (isSupertype) 
            {
                agentSupertypes.insert(currentType);
            }
            else 
            {
                agentSupertypes.erase(currentType);
            }
        }

        // check if all supertypes share a common parent (it is necessary, since types
        // can only have one parent)
        bool allHaveSameParent = true;
        auto parentType = (*(agentSupertypes.begin()))->supertype.lock();

        for (const auto& agentSupertype : agentSupertypes)
        {
            if (agentSupertype->supertype.lock() != parentType) 
            {
                allHaveSameParent = false;
                break;
            }
        }

        // if all have same parent, add AGENT type between supertype and members
        // of agentSupertypes
        if ( allHaveSameParent ) 
        {
            d.createType("AGENT", parentType->name);
            auto agentType = d.getType( "AGENT" );

            for (const auto& agentSupertype : agentSupertypes)
            {
                for (auto it = parentType->subtypes.begin(); it != parentType->subtypes.end();)
                {
                    if (it->lock() == agentSupertype)
                    {
                        parentType->subtypes.erase(it);
                        connect_types(agentType, agentSupertype);
                        //agentType->insertSubtype( agentSupertype );
                        break;
                    }
                    ++it;
                }
            }
        }
    }
}

static void addFunctions(const parser::multiagent::ConcurrencyDomain& d, Domain& cd)
{
    for (const auto& f : d.funcs)
        cd.createFunction(f->name, f->returnType, d.typeList(*f));
}

struct ConditionClassification
{
    unsigned numActionParams;
    unsigned lastParamId;

    std::map<unsigned, std::weak_ptr<Condition>> paramToCond; // parameter number to condition that declares it (forall, exists)

    CondVec posConcConds; // conditions that include positive concurrency
    CondVec negConcConds; // conditions that include negative concurrency
    CondVec normalConds; // conditions that do not include concurrency constraints

    CondVec checkedConds; // conditions that have been checked and cannot be checked again (i.e. exists)

    ConditionClassification( unsigned numParams)
        : numActionParams( numParams ), lastParamId( numParams - 1 ) {
    }

    ~ConditionClassification() = default;
};

void addNoopAction( parser::multiagent::ConcurrencyDomain& d )
{
    std::string actionName = "NOOP";
    auto a = std::make_shared<parser::multiagent::ConcurrentAction>(actionName);
    a->params.emplace_back(d.types.index("AGENT"));
    a->pre = std::make_shared<And>();
    a->eff = std::make_shared<And>();
    d.actions.insert(a);
    d.addConcurrencyPredicateFromAction(*a);
}

void addOriginalPredicates(const parser::multiagent::ConcurrencyDomain& d, Domain& cd)
{
    for (const auto& pred : d.preds)
    {
        if (d.cpreds.index(pred->name) == -1)
        {
            cd.createPredicate(pred->name, d.typeList(*pred));
        }
        else
        {
            cd.createPredicate("ACTIVE-" + pred->name, d.typeList(*pred));
            cd.createPredicate("REQ-NEG-" + pred->name, d.typeList(*pred));
        }
    }
}

void addStatePredicates(Domain& cd)
{
    cd.createPredicate( "FREE-BLOCK" );
    cd.createPredicate( "SELECTING" );
    cd.createPredicate( "APPLYING" );
    cd.createPredicate( "RESETTING" );

    cd.createPredicate( "FREE-AGENT", StringVec( 1, "AGENT" ) );
    cd.createPredicate( "BUSY-AGENT", StringVec( 1, "AGENT" ) );
    cd.createPredicate( "DONE-AGENT", StringVec( 1, "AGENT" ) );
}

void addAgentOrderPredicates(Domain& cd)
{
    auto sv = StringVec( 1, "AGENT" );
    sv.emplace_back("AGENT-ORDER-COUNT" );
    cd.createPredicate( "AGENT-ORDER", sv );

    cd.createPredicate( "PREV-AGENT-ORDER-COUNT", StringVec( 2, "AGENT-ORDER-COUNT" ) );
    cd.createPredicate( "NEXT-AGENT-ORDER-COUNT", StringVec( 2, "AGENT-ORDER-COUNT" ) );
    cd.createPredicate( "CURRENT-AGENT-ORDER-COUNT", StringVec( 1, "AGENT-ORDER-COUNT" ) );
}

void addJointActionSizePredicates(Domain& cd, int maxJointActionSize)
{
    cd.createPredicate( "PREV-ATOMIC-ACTION-COUNT", StringVec( 2, "ATOMIC-ACTION-COUNT" ) );
    cd.createPredicate( "NEXT-ATOMIC-ACTION-COUNT", StringVec( 2, "ATOMIC-ACTION-COUNT" ) );
    cd.createPredicate( "CURRENT-ATOMIC-ACTION-COUNT", StringVec( 1, "ATOMIC-ACTION-COUNT" ) );
}

void addPredicates(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, bool useAgentOrder, int maxJointActionSize)
{
    addStatePredicates( cd );
    addOriginalPredicates( d, cd );

    if ( useAgentOrder ) {
        addAgentOrderPredicates( cd );
    }

    if ( maxJointActionSize > 0 ) {
        addJointActionSizePredicates( cd, maxJointActionSize );
    }
}

std::shared_ptr<Condition> replaceConcurrencyPredicates(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, const std::shared_ptr<Condition>& cond, std::string& replacementPrefix, bool turnNegative )
{
    auto a = std::dynamic_pointer_cast<And>(cond);
    if (a) 
    {
        for (auto& cond : a->conds)
	        cond = replaceConcurrencyPredicates( d, cd, cond, replacementPrefix, turnNegative );
        return a;
    }

    auto e = std::dynamic_pointer_cast<Exists>( cond );
    if ( e ) {
        e->cond = replaceConcurrencyPredicates( d, cd, e->cond, replacementPrefix, turnNegative );
        return e;
    }

    auto f = std::dynamic_pointer_cast<Forall>( cond );
    if ( f ) {
        f->cond = replaceConcurrencyPredicates( d, cd, f->cond, replacementPrefix, turnNegative );
        return f;
    }

    auto i = std::dynamic_pointer_cast<Increase>( cond );
    if ( i ) {
        return i;
    }

    auto n = std::dynamic_pointer_cast<Not>( cond );
    if ( n ) {
        n->cond = std::dynamic_pointer_cast<Ground>( replaceConcurrencyPredicates( d, cd, n->cond, replacementPrefix, turnNegative ) );
        return n;
    }

    auto g = std::dynamic_pointer_cast<Ground>( cond );
    if ( g ) 
    {
        if ( d.cpreds.index( g->name ) != -1 ) 
        {
            std::string newName = replacementPrefix + g->name;
            g->name = newName;
            g->lifted = cd.preds.get( newName );
            if ( turnNegative ) {
                return std::make_shared<Not>(g);
            }
        }
        return g;
    }

    auto o = std::dynamic_pointer_cast<Or>( cond );
    if ( o ) {
        o->first = replaceConcurrencyPredicates( d, cd, o->first, replacementPrefix, turnNegative );
        o->second = replaceConcurrencyPredicates( d, cd, o->second, replacementPrefix, turnNegative );
        return o;
    }

    auto w = std::dynamic_pointer_cast<When>( cond );
    if ( w ) {
        w->pars = replaceConcurrencyPredicates( d, cd, w->pars, replacementPrefix, turnNegative );
        w->cond = replaceConcurrencyPredicates( d, cd, w->cond, replacementPrefix, turnNegative );
        return w;
    }

    return nullptr;
}

int getDominantGroundTypeForCondition(const parser::multiagent::ConcurrencyDomain& d, const std::shared_ptr<Condition>& cond)
{
    auto a = std::dynamic_pointer_cast<And>( cond );
    if ( a ) 
    {
        int finalRes = 0;
        for (const auto& cond : a->conds)
        {
            finalRes = getDominantGroundTypeForCondition( d, cond);
            if ( finalRes == -1 || finalRes == 1 ) {
                break;
            }
        }
        return finalRes;
    }

    auto e = std::dynamic_pointer_cast<Exists>( cond );
    if ( e ) {
        return getDominantGroundTypeForCondition( d, e->cond );
    }

    auto f = std::dynamic_pointer_cast<Forall>( cond );
    if ( f ) {
        return getDominantGroundTypeForCondition( d, f->cond );
    }

    auto n = std::dynamic_pointer_cast<Not>( cond );
    if ( n ) {
        auto gn = std::dynamic_pointer_cast<Ground>( n->cond );

        if ( d.cpreds.index( gn->name ) != -1 ) {
            return -1;
        }
        else {
            return -2;
        }
    }

    auto g = std::dynamic_pointer_cast<Ground>( cond );
    if ( g ) 
    {
        if ( d.cpreds.index( g->name ) != -1 ) {
            return 1;
        }
        else {
            return 2;
        }
    }

    return 0;
}

std::pair<std::shared_ptr<Condition>, int> createFullNestedCondition(const parser::multiagent::ConcurrencyDomain& d, const Domain& cd, const Ground& g, int groundType, ConditionClassification& condClassif, const CondVec& nestedConditions )
{
    std::shared_ptr<Condition> finalCond;
    int finalGroundType = groundType;
    std::shared_ptr<And> lastAnd = nullptr;

    for (const auto& nestedCondition : nestedConditions)
    {
        std::shared_ptr<Condition> newCond;
        std::shared_ptr<And> currentAnd;

        auto f = std::dynamic_pointer_cast<Forall>(nestedCondition);
        if ( f ) 
        {
            auto nf = std::make_shared<Forall>();
            nf->params = IntVec( f->params );
            nf->cond = std::make_shared<And>();

            newCond = nf;
            currentAnd = std::dynamic_pointer_cast<And>( nf->cond );
        }

        auto e = std::dynamic_pointer_cast<Exists>(nestedCondition);
        if ( e ) 
        {
            std::shared_ptr<Exists> ne;

            if (std::dynamic_pointer_cast<And>( e->cond ) ) 
            {
                ne = std::dynamic_pointer_cast<Exists>( e->copy( d ) );
            }
            else {
                ne = std::make_shared<Exists>();
                ne->params = IntVec( e->params );

                auto newAnd = std::make_shared<And>();
                newAnd->add( e->cond->copy(d));

                ne->cond = newAnd;
            }

            condClassif.checkedConds.emplace_back(nestedCondition);

            // the ground type can be changed if there is a concurrency predicate
            // inside the exists
            if ( groundType != -1 && groundType != 1 ) 
            {
                finalGroundType = getDominantGroundTypeForCondition(d, nestedCondition);
            }

            newCond = ne;
            currentAnd = nullptr; // do not nest anything more inside this structure
        }

        if ( newCond ) {
            if ( !finalCond ) {
                finalCond = newCond;
            }

            if ( lastAnd ) {
                lastAnd->add( newCond );
            }

            lastAnd = currentAnd;

            if ( !lastAnd ) {
                break;
            }
        }
    }

    if ( lastAnd ) { // just non null in the case of forall
        switch ( finalGroundType ) {
            case -2:
            {
                auto cg = std::dynamic_pointer_cast<Ground>(g.copy(cd));
                lastAnd->add(std::make_shared<Not>(cg));
                break;
            }
            case -1:
            case 1:
                lastAnd->add(g.copy(d));
                break;
            case 2:
                lastAnd->add(g.copy(cd));
                break;
        }
    }

    return std::make_pair( finalCond, finalGroundType );
}

bool isGroundClassified(const Ground& g, const ConditionClassification& condClassif)
{
    for (unsigned int paramId : g.params)
    {
	    if ( paramId >= condClassif.numActionParams ) { // non-action parameter (introduced by forall or exists)
            auto cond = condClassif.paramToCond.at(paramId).lock();
            if (std::ranges::find(condClassif.checkedConds, cond) != condClassif.checkedConds.end() ) 
				return true;
        }
    }

    return false;
}

void getNestedConditionsForGround(CondVec& nestedConditions, const Ground& g, const ConditionClassification& condClassif )
{
    std::shared_ptr<Condition> lastNestedCondition;

    std::set< int > sortedGroundParams( g.params.begin(), g.params.end() ); // sort to respect nested order

    for (unsigned int paramId : sortedGroundParams)
    {
	    if ( paramId >= condClassif.numActionParams ) { // non-action parameter (introduced by forall or exists)
            auto cond = condClassif.paramToCond.at(paramId).lock();
            if ( cond != lastNestedCondition ) {
                nestedConditions.emplace_back( cond );
                lastNestedCondition = cond;
            }
        }
    }
}

void classifyGround(const parser::multiagent::ConcurrencyDomain& d, const Domain& cd, const Ground& g, int groundType, ConditionClassification & condClassif )
{
    if ( !isGroundClassified( g, condClassif ) ) {
        CondVec nestedConditions;
        getNestedConditionsForGround( nestedConditions, g, condClassif );

        if (nestedConditions.empty()) 
        {
            switch ( groundType )
        	{
                case -2:
                {
                    auto cg = std::dynamic_pointer_cast<Ground>(g.copy(cd));
                    condClassif.normalConds.emplace_back(std::make_shared<Not>(cg));
                    break;
                }
                case -1:
                    condClassif.negConcConds.emplace_back(g.copy(d));
                    break;
                case 1:
                    condClassif.posConcConds.emplace_back(g.copy(d));
                    break;
                case 2:
                    condClassif.normalConds.emplace_back(g.copy(cd));
                    break;
            }
        }
        else {
            auto result = createFullNestedCondition( d, cd, g, groundType, condClassif, nestedConditions );
            auto nestedCondition = result.first;
            groundType = result.second;

            switch ( groundType ) {
                case -2:
                case 2:
                    condClassif.normalConds.emplace_back( nestedCondition );
                    break;
                case -1:
                    condClassif.negConcConds.emplace_back( nestedCondition );
                    break;
                case 1:
                    condClassif.posConcConds.emplace_back( nestedCondition );
                    break;
            }
        }
    }
}

void getClassifiedConditions(const parser::multiagent::ConcurrencyDomain& d, const Domain& cd, const std::shared_ptr<Condition>& cond, ConditionClassification& condClassif)
{
    auto a = std::dynamic_pointer_cast<And>( cond );
    for ( unsigned i = 0; a && i < a->conds.size(); ++i ) 
    {
        getClassifiedConditions( d, cd, a->conds[i], condClassif );
    }

    auto e = std::dynamic_pointer_cast<Exists>( cond );
    if ( e ) {
        for ( unsigned i = 0; i < e->params.size(); ++i ) {
            ++condClassif.lastParamId;
            condClassif.paramToCond[ condClassif.lastParamId ] = e;
        }

        getClassifiedConditions( d, cd, e->cond, condClassif );

        condClassif.lastParamId -= e->params.size();
    }

    auto f = std::dynamic_pointer_cast<Forall>( cond );
    if ( f ) {
        for ( unsigned i = 0; i < f->params.size(); ++i ) {
            ++condClassif.lastParamId;
            condClassif.paramToCond[ condClassif.lastParamId ] = f;
        }

        getClassifiedConditions( d, cd, f->cond, condClassif );

        condClassif.lastParamId -= f->params.size();
    }

    auto g = std::dynamic_pointer_cast<Ground>( cond );
    if ( g ) {
        int category = d.cpreds.index( g->name ) != -1 ? 1 : 2;
        classifyGround( d, cd, *g, category, condClassif );
    }

    auto n = std::dynamic_pointer_cast<Not>( cond );
    if ( n ) {
        auto ng = std::dynamic_pointer_cast<Ground>( n->cond );
        if ( ng ) {
            int category = d.cpreds.index( ng->name ) != -1 ? -1 : -2;
            classifyGround( d, cd, *ng, category, condClassif );
        }
        else {
            getClassifiedConditions( d, cd, n->cond, condClassif );
        }
    }
}

void addSelectAction(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, int actionId, bool useAgentOrder, int maxJointActionSize, const ConditionClassification& condClassif)
{
    auto originalAction = d.actions[actionId];

    std::string actionName = "SELECT-" + originalAction->name;

    auto newAction = cd.createAction( actionName, d.typeList( *originalAction ) );
    size_t numActionParams = newAction->params.size();

    // preconditions
    cd.addPre( false, actionName, "SELECTING" );
    cd.addPre( false, actionName, "FREE-AGENT", IntVec( 1, 0 ) );
    cd.addPre( true, actionName, "REQ-NEG-" + originalAction->name, incvec( 0, numActionParams ) );

    auto actionPre = std::dynamic_pointer_cast<And>( newAction->pre );
    std::string replacementPrefix = "ACTIVE-";

    for (const auto& normalCond : condClassif.normalConds)
        actionPre->add(normalCond);

    for (const auto& negConcCond : condClassif.negConcConds)
    {
        auto replacedCondition = replaceConcurrencyPredicates( d, cd, negConcCond->copy(d), replacementPrefix, true );
        actionPre->add( replacedCondition );
    }

    // effects
    cd.addEff( true, actionName, "FREE-AGENT", IntVec( 1, 0 ) );
    cd.addEff( false, actionName, "BUSY-AGENT", IntVec( 1, 0 ) );
    cd.addEff( false, actionName, "ACTIVE-" + originalAction->name, incvec( 0, numActionParams ) );

    auto actionEff = std::dynamic_pointer_cast<And>( newAction->eff );
    replacementPrefix = "REQ-NEG-";

    for (const auto& negConcCond : condClassif.negConcConds)
    {
        auto replacedCondition = replaceConcurrencyPredicates( d, cd, negConcCond->copy(d), replacementPrefix, false );
        actionEff->add( replacedCondition );
    }

    if ( useAgentOrder ) {
        newAction->addParams( cd.convertTypes( StringVec( 2, "AGENT-ORDER-COUNT" ) ) );

        IntVec orderParams = IntVec( 1, 0 ); // agent parameter
        orderParams.push_back( numActionParams ); // num of parameter corresponding to AGENT-ORDER-COUNT (just added in previous line)
        cd.addPre( false, actionName, "AGENT-ORDER", orderParams );
        cd.addPre( false, actionName, "NEXT-AGENT-ORDER-COUNT", incvec( numActionParams, numActionParams + 2 ) );
        cd.addPre( false, actionName, "CURRENT-AGENT-ORDER-COUNT", IntVec( 1, static_cast<int>(numActionParams) ));

        cd.addEff( true, actionName, "CURRENT-AGENT-ORDER-COUNT", IntVec( 1, static_cast<int>(numActionParams)) );
        cd.addEff( false, actionName, "CURRENT-AGENT-ORDER-COUNT", IntVec( 1, static_cast<int>(numActionParams) + 1 ) );

        numActionParams += 2;
    }

    if ( maxJointActionSize > 0 ) 
    {
        newAction->addParams( cd.convertTypes( StringVec( 2, "ATOMIC-ACTION-COUNT" ) ) );

        cd.addPre( false, actionName, "NEXT-ATOMIC-ACTION-COUNT", incvec( numActionParams, numActionParams + 2 ) );
        cd.addPre( false, actionName, "CURRENT-ATOMIC-ACTION-COUNT", IntVec( 1, static_cast<int>(numActionParams)) );

        cd.addEff( true, actionName, "CURRENT-ATOMIC-ACTION-COUNT", IntVec( 1, static_cast<int>(numActionParams)) );
        cd.addEff( false, actionName, "CURRENT-ATOMIC-ACTION-COUNT", IntVec( 1, static_cast<int>(numActionParams) + 1 ) );
    }
}

void addDoAction(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, int actionId, ConditionClassification & condClassif )
{
    auto originalAction = d.actions[actionId];

    std::string actionName = "DO-" + originalAction->name;

    auto newAction = cd.createAction( actionName, d.typeList( *originalAction ) );
    size_t numActionParams = newAction->params.size();

    // preconditions
    cd.addPre( false, actionName, "APPLYING" );
    cd.addPre( false, actionName, "BUSY-AGENT", IntVec( 1, 0 ) );
    cd.addPre( false, actionName, "ACTIVE-" + originalAction->name, incvec( 0, numActionParams ) );

    auto newActionPre = std::dynamic_pointer_cast<And>( newAction->pre );
    std::string replacementPrefix = "ACTIVE-";

    for (const auto& posConcCond : condClassif.posConcConds)
    {
        auto replacedCondition = replaceConcurrencyPredicates( d, cd, posConcCond->copy(d), replacementPrefix, false );
        newActionPre->add( replacedCondition );
    }

    // effects
    cd.addEff( true, actionName, "BUSY-AGENT", IntVec( 1, 0 ) );
    cd.addEff( false, actionName, "DONE-AGENT", IntVec( 1, 0 ) );

    auto newActionEff = std::dynamic_pointer_cast<And>( newAction->eff );

    if (auto originalActionEff = std::dynamic_pointer_cast<And>( originalAction->eff ) ) 
    {
        for ( unsigned i = 0; i < originalActionEff->conds.size(); ++i ) 
        {
            newActionEff->add( originalActionEff->conds[i]->copy(d));
        }
    }
    else if ( originalAction->eff != nullptr )
    {
        newActionEff->add( originalAction->eff->copy(d));
    }

    newAction->eff = replaceConcurrencyPredicates( d, cd, newAction->eff, replacementPrefix, false );
}

void addEndAction(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, int actionId, bool useAgentOrder, int maxJointActionSize, const ConditionClassification& condClassif )
{
    auto originalAction = d.actions[actionId];

    std::string actionName = "END-" + originalAction->name;

    auto newAction = cd.createAction( actionName, d.typeList( *originalAction ) );
    unsigned numActionParams = newAction->params.size();

    // preconditions
    cd.addPre( false, actionName, "RESETTING" );
    cd.addPre( false, actionName, "DONE-AGENT", IntVec( 1, 0 ) );
    cd.addPre( false, actionName, "ACTIVE-" + originalAction->name, incvec( 0, numActionParams ) );

    // effects
    cd.addEff( true, actionName, "DONE-AGENT", IntVec( 1, 0 ) );
    cd.addEff( false, actionName, "FREE-AGENT", IntVec( 1, 0 ) );
    cd.addEff( true, actionName, "ACTIVE-" + originalAction->name, incvec( 0, numActionParams ) );

    auto actionEff = std::dynamic_pointer_cast<And>( newAction->eff );
    std::string replacementPrefix = "REQ-NEG-";

    for (const auto& negConcCond : condClassif.negConcConds)
    {
        auto replacedCondition = replaceConcurrencyPredicates( d, cd, negConcCond->copy(d), replacementPrefix, true );
        actionEff->add( replacedCondition );
    }

    if ( useAgentOrder ) 
    {
        newAction->addParams( cd.convertTypes( StringVec( 2, "AGENT-ORDER-COUNT" ) ) );

        cd.addPre( false, actionName, "PREV-AGENT-ORDER-COUNT", incvec( numActionParams, numActionParams + 2 ) );
        cd.addPre( false, actionName, "CURRENT-AGENT-ORDER-COUNT", IntVec( 1, static_cast<int>(numActionParams)) );

        cd.addEff( true, actionName, "CURRENT-AGENT-ORDER-COUNT", IntVec( 1, static_cast<int>(numActionParams) ) );
        cd.addEff( false, actionName, "CURRENT-AGENT-ORDER-COUNT", IntVec( 1, static_cast<int>(numActionParams) + 1 ) );

        numActionParams += 2;
    }

    if ( maxJointActionSize > 0 ) 
    {
        newAction->addParams( cd.convertTypes( StringVec( 2, "ATOMIC-ACTION-COUNT" ) ) );

        cd.addPre( false, actionName, "PREV-ATOMIC-ACTION-COUNT", incvec( numActionParams, numActionParams + 2 ) );
        cd.addPre( false, actionName, "CURRENT-ATOMIC-ACTION-COUNT", IntVec( 1, static_cast<int>(numActionParams)) );

        cd.addEff( true, actionName, "CURRENT-ATOMIC-ACTION-COUNT", IntVec( 1, static_cast<int>(numActionParams)) );
        cd.addEff( false, actionName, "CURRENT-ATOMIC-ACTION-COUNT", IntVec( 1, static_cast<int>(numActionParams) + 1 ) );
    }
}

void addStartAction(Domain& cd)
{
    std::string actionName = "START";
    cd.createAction(actionName);
    cd.addPre( false, actionName, "FREE-BLOCK" );
    cd.addEff( true, actionName, "FREE-BLOCK" );
    cd.addEff( false, actionName, "SELECTING" );
}

void addApplyAction(Domain& cd)
{
    std::string actionName = "APPLY";
    cd.createAction(actionName);
    cd.addPre( false, actionName, "SELECTING" );
    cd.addEff( true, actionName, "SELECTING" );
    cd.addEff( false, actionName, "APPLYING" );
}

void addResetAction(Domain& cd)
{
    std::string actionName = "RESET";
    cd.createAction(actionName);
    cd.addPre( false, actionName, "APPLYING" );
    cd.addEff( true, actionName, "APPLYING" );
    cd.addEff( false, actionName, "RESETTING" );
}

void addFinishAction(Domain& cd)
{
    std::string actionName = "FINISH";
    auto action = cd.createAction(actionName);
    cd.addPre( false, actionName, "RESETTING" );
    cd.addEff( true, actionName, "RESETTING" );
    cd.addEff( false, actionName, "FREE-BLOCK" );

    auto f = std::make_shared<Forall>();
    f->params = cd.convertTypes( StringVec( 1, "AGENT" ) );
    f->cond = std::make_shared<Ground>( cd.preds.get( "FREE-AGENT" ), incvec( 0, f->params.size() ) );

    auto a = std::dynamic_pointer_cast<And>( action->pre );
    a->add(f);
}

void addStateChangeActions( Domain& cd )
{
    addStartAction( cd );
    addApplyAction( cd );
    addResetAction( cd );
    addFinishAction( cd );
}

void addActions(const parser::multiagent::ConcurrencyDomain& d, Domain& cd, bool useAgentOrder, int maxJointActionSize )
{
    addStateChangeActions( cd );

    // select, do and end actions for each original action
    for ( unsigned i = 0; i < d.actions.size(); ++i ) 
    {
        ConditionClassification condClassif( d.actions[i]->params.size() );
        getClassifiedConditions( d, cd, d.actions[i]->pre, condClassif );

        addSelectAction( d, cd, i, useAgentOrder, maxJointActionSize, condClassif );
        addDoAction( d, cd, i, condClassif );
        addEndAction( d, cd, i, useAgentOrder, maxJointActionSize, condClassif );
    }
}

std::shared_ptr<Domain> createClassicalDomain(const parser::multiagent::ConcurrencyDomain& d, bool useAgentOrder, int maxJointActionSize )
{
    auto cd = std::make_shared<Domain>();
    cd->name = d.name;
    cd->condeffects = cd->cons = cd->typed = cd->neg = cd->equality = cd->universal = true;
    cd->costs = d.costs;

    addTypes(d, *cd, useAgentOrder, maxJointActionSize );
    addFunctions( d, *cd);
    addPredicates( d, *cd, useAgentOrder, maxJointActionSize );
    addActions( d, *cd, useAgentOrder, maxJointActionSize );

    return cd;
}

std::shared_ptr<Instance> createTransformedInstance(Domain& cd, const Instance& ins, bool useAgentOrder, int maxJointActionSize )
{
    auto cins = std::make_shared<Instance>(cd);
    cins->name = ins.name;
    cins->metric = ins.metric;

    // create initial state
    auto agentType = cd.types.get( "AGENT" );
    cins->addInit( "FREE-BLOCK" );
    for ( unsigned i = 0; i < agentType->noObjects(); ++i ) {
        cins->addInit( "FREE-AGENT", StringVec( 1, agentType->object(i).first ) );
    }

    for (const auto& i : ins.init)
    {
        if ( cd.preds.index(i->name ) >= 0 ) 
        {
            cins->addInit(i->name, cd.objectList( *i) );
        }
        else if (auto gfd = std::dynamic_pointer_cast<GroundFunc<double>>(i) ) 
        {
            cins->addInit( gfd->name, gfd->value, cd.objectList( *gfd ) );
        }
        else if (auto gfi = std::dynamic_pointer_cast<GroundFunc<int>>(i) ) 
        {
            cins->addInit( gfi->name, gfi->value, cd.objectList( *gfi ) );
        }
    }

    // create goal state
    cins->addGoal( "FREE-BLOCK" );
    for (const auto& i : ins.goal)
        cins->addGoal(i->name, cd.objectList( *i) );

    if ( useAgentOrder ) 
    {
        for ( unsigned i = 1; i <= agentType->noObjects() + 1; ++i ) 
        {
            std::stringstream ss;
            ss << "AGENT-COUNT" << i;
            cins->addObject( ss.str(), "AGENT-ORDER-COUNT" );
        }

        if ( agentType->noObjects() > 0 ) 
        {
            cins->addInit( "CURRENT-AGENT-ORDER-COUNT", StringVec( 1, "AGENT-COUNT1" ) );
        }

        for ( unsigned i = 1; i <= agentType->noObjects(); ++i ) {
            std::stringstream ss;
            ss << "AGENT-COUNT" << i;

            StringVec sv( 1, agentType->object(i - 1).first );
            sv.push_back( ss.str() );
            cins->addInit( "AGENT-ORDER", sv );

            std::stringstream ss2;
            ss2 << "AGENT-COUNT" << i + 1;

            StringVec sv2( 1, ss.str() );
            sv2.push_back( ss2.str() );
            cins->addInit( "NEXT-AGENT-ORDER-COUNT", sv2 );

            StringVec sv3( 1, ss2.str() );
            sv3.push_back( ss.str() );
            cins->addInit( "PREV-AGENT-ORDER-COUNT", sv3 );
        }
    }

    if ( maxJointActionSize > 0 ) 
    {
        for ( int i = 0; i <= maxJointActionSize; ++i ) 
        {
            std::stringstream ss;
            ss << "ATOMIC-COUNT" << i;
            cins->addObject( ss.str(), "ATOMIC-ACTION-COUNT" );
        }

        cins->addInit( "CURRENT-ATOMIC-ACTION-COUNT", StringVec( 1, "ATOMIC-COUNT0" ) );

        for ( int i = 0; i < maxJointActionSize; ++i ) {
            std::stringstream ss, ss2;
            ss << "ATOMIC-COUNT" << i;
            ss2 << "ATOMIC-COUNT" << i + 1;

            StringVec sv1( 1, ss.str() );
            sv1.push_back( ss2.str() );
            cins->addInit( "NEXT-ATOMIC-ACTION-COUNT", sv1 );

            StringVec sv2( 1, ss2.str() );
            sv2.push_back( ss.str() );
            cins->addInit( "PREV-ATOMIC-ACTION-COUNT", sv2 );
        }
    }

    return cins;
}

int main(int argc, char* argv[])
{
    ProgramParams pp(argc, argv);

    if (pp.help)
        showHelp();

    // load multiagent domain and instance
    auto d = std::make_unique<parser::multiagent::ConcurrencyDomain>(pp.domain);

    addAgentType(*d);

    // add no-op action that will be used in the transformation
    if (pp.agentOrder)
        addNoopAction(*d);

    auto ins = std::make_unique<Instance>(*d, pp.ins);

    // create classical/single-agent domain
    auto cd = createClassicalDomain(*d, pp.agentOrder, pp.maxJointActionSize);
    std::cout << *cd;

    auto ci = createTransformedInstance(*cd, *ins, pp.agentOrder, pp.maxJointActionSize);
    std::cerr << *ci;

    return 0;
}