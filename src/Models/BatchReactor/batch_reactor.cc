// batch_reactor.cc
// Implements the BatchReactor class
#include <sstream>
#include <cmath>

#include <boost/lexical_cast.hpp>

#include "cyc_limits.h"
#include "context.h"
#include "error.h"
#include "logger.h"

#include "batch_reactor.h"

namespace cycamore {

// static members
std::map<BatchReactor::Phase, std::string> BatchReactor::phase_names_ =
    std::map<Phase, std::string>();

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BatchReactor::BatchReactor(cyclus::Context* ctx)
    : cyclus::FacilityModel(ctx),
      cyclus::Model(ctx),
      process_time_(1),
      preorder_time_(0),
      refuel_time_(0),
      start_time_(-1),
      n_batches_(1),
      n_load_(1),
      n_reserves_(1),
      batch_size_(1),
      in_commodity_(""),
      in_recipe_(""),
      out_commodity_(""),
      out_recipe_(""),
      phase_(INITIAL),
      ics_(InitCond(0, 0, 0)) {
  reserves_.set_capacity(cyclus::kBuffInfinity);
  core_.set_capacity(cyclus::kBuffInfinity);
  storage_.set_capacity(cyclus::kBuffInfinity);
  if (phase_names_.empty()) {
    SetUpPhaseNames_();
  }
  spillover_ = cyclus::Material::CreateBlank(0);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BatchReactor::~BatchReactor() {}
  
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BatchReactor::schema() {
  return
    "  <!-- cyclus::Material In/Out  -->           \n"
    "  <element name=\"fuel_input\">               \n"
    "   <ref name=\"incommodity\"/>                \n"
    "   <ref name=\"inrecipe\"/>                   \n"
    "  </element>                                  \n"
    "  <element name=\"fuel_output\">              \n"
    "   <ref name=\"outcommodity\"/>               \n"
    "   <ref name=\"outrecipe\"/>                  \n"
    "  </element>                                  \n"
    "                                              \n"
    "  <!-- Facility Parameters -->                \n"
    "  <interleave>                                \n"
    "  <element name=\"processtime\">              \n"
    "    <data type=\"nonNegativeInteger\"/>       \n"
    "  </element>                                  \n"
    "  <element name=\"nbatches\">                 \n"
    "    <data type=\"nonNegativeInteger\"/>       \n"
    "  </element>                                  \n"
    "  <element name =\"batchsize\">               \n"
    "    <data type=\"double\"/>                   \n"
    "  </element>                                  \n"
    "  <optional>                                  \n"
    "    <element name =\"refueltime\">            \n"
    "      <data type=\"nonNegativeInteger\"/>     \n"
    "    </element>                                \n"
    "  </optional>                                 \n"
    "  <optional>                                  \n"
    "    <element name =\"orderlookahead\">        \n"
    "      <data type=\"nonNegativeInteger\"/>     \n"
    "    </element>                                \n"
    "  </optional>                                 \n"
    "  <optional>                                  \n"
    "    <element name =\"norder\">                \n"
    "      <data type=\"nonNegativeInteger\"/>     \n"
    "    </element>                                \n"
    "  </optional>                                 \n"
    "  <optional>                                  \n"
    "    <element name =\"nreload\">               \n"
    "      <data type=\"nonNegativeInteger\"/>     \n"
    "    </element>                                \n"
    "  </optional>                                 \n"
    "  <optional>                                  \n"
    "    <element name =\"initial_condition\">     \n"
    "      <optional>                              \n"
    "        <element name =\"nreserves\">         \n"
    "          <data type=\"nonNegativeInteger\"/> \n"
    "        </element>                            \n"
    "      </optional>                             \n"
    "      <optional>                              \n"
    "        <element name =\"ncore\">             \n"
    "          <data type=\"nonNegativeInteger\"/> \n"
    "        </element>                            \n"
    "      </optional>                             \n"
    "      <optional>                              \n"
    "        <element name =\"nstorage\">          \n"
    "          <data type=\"nonNegativeInteger\"/> \n"
    "        </element>                            \n"
    "      </optional>                             \n"
    "    </element>                                \n"
    "  </optional>                                 \n"
    "  </interleave>                               \n"
    "                                              \n"
    "  <!-- Power Production  -->                  \n"
    "  <element name=\"commodity_production\">     \n"
    "   <element name=\"commodity\">               \n"
    "     <data type=\"string\"/>                  \n"
    "   </element>                                 \n"
    "   <element name=\"capacity\">                \n"
    "     <data type=\"double\"/>                  \n"
    "   </element>                                 \n"
    "   <element name=\"cost\">                    \n"
    "     <data type=\"double\"/>                  \n"
    "   </element>                                 \n"
    "  </element>                                  \n";
};

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::InitModuleMembers(cyclus::QueryEngine* qe) {  
  using boost::lexical_cast;
  using cyclus::Commodity;
  using cyclus::CommodityProducer;
  using cyclus::GetOptionalQuery;
  using cyclus::QueryEngine;
  using std::string;
  
  // in/out
  QueryEngine* input = qe->QueryElement("fuel_input");
  in_commodity(input->GetElementContent("incommodity"));
  in_recipe(input->GetElementContent("inrecipe"));
  
  QueryEngine* output = qe->QueryElement("fuel_output");
  out_commodity(output->GetElementContent("outcommodity"));
  out_recipe(output->GetElementContent("outrecipe"));

  // facility data required
  string data;
  data = qe->GetElementContent("processtime");
  process_time(lexical_cast<int>(data));
  data = qe->GetElementContent("nbatches");
  n_batches(lexical_cast<int>(data));
  data = qe->GetElementContent("batchsize");
  batch_size(lexical_cast<double>(data));

  // facility data optional  
  int time =
      GetOptionalQuery<int>(qe, "refueltime", refuel_time());
  refuel_time(time);
  time =
      GetOptionalQuery<int>(qe, "orderlookahead", preorder_time());
  preorder_time(time);

  int n = 
      GetOptionalQuery<int>(qe, "nreload", n_load());
  n_load(n);
  n = GetOptionalQuery<int>(qe, "norder", n_load());
  n_reserves(n);

  // initial condition
  int nreserves = 0;
  int ncore = 0;
  int nstorage = 0;
  if (qe->NElementsMatchingQuery("initial_condition") > 0) {
    QueryEngine* ic = qe->QueryElement("initial_condition");
    nreserves = GetOptionalQuery<int>(ic, "nreserves", 0);
    ncore = GetOptionalQuery<int>(ic, "ncore", 0);
    nstorage = GetOptionalQuery<int>(ic, "nstorage", 0);
  }
  ics(InitCond(nreserves, ncore, nstorage));
      
  // commodity production
  QueryEngine* commodity = qe->QueryElement("commodity_production");
  Commodity commod(commodity->GetElementContent("commodity"));
  AddCommodity(commod);
  data = commodity->GetElementContent("capacity");
  CommodityProducer::SetCapacity(commod,
                                         lexical_cast<double>(data));
  data = commodity->GetElementContent("cost");
  CommodityProducer::SetCost(commod,
                                     lexical_cast<double>(data));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Model* BatchReactor::Clone() {
  BatchReactor* m = new BatchReactor(context());
  m->InitFrom(this);

  // in/out
  m->in_commodity(in_commodity());
  m->out_commodity(out_commodity());
  m->in_recipe(in_recipe());
  m->out_recipe(out_recipe());

  // facility params
  m->process_time(process_time());
  m->preorder_time(preorder_time());
  m->refuel_time(refuel_time());
  m->n_batches(n_batches());
  m->n_load(n_load());
  m->n_reserves(n_reserves());
  m->batch_size(batch_size());

  // commodity production
  m->CopyProducedCommoditiesFrom(this);

  // ics
  m->ics(ics());
  
  return m;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BatchReactor::str() {
  std::stringstream ss;
  ss << cyclus::FacilityModel::str();
  ss << " has facility parameters {"
     << ", Process Time = " << process_time()
     << ", Refuel Time = " << refuel_time()
     << ", Core Loading = " << n_batches() * batch_size()
     << ", Batches Per Core = " << n_batches()
     << ", converts commodity '" << in_commodity()
     << "' into commodity '" << out_commodity()
     << "'}";
  return ss.str();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Deploy(cyclus::Model* parent) {
  using cyclus::Material;

  FacilityModel::Deploy(parent);
  phase(INITIAL);
  spillover_ = Material::CreateBlank(0);

  Material::Ptr mat;
  for (int i = 0; i < ics_.n_reserves; ++i) {
    mat = Material::Create(this, batch_size(), context()->GetRecipe(in_recipe_));
    reserves_.Push(mat);
  }
  for (int i = 0; i < ics_.n_core; ++i) {
    mat = Material::Create(this, batch_size(), context()->GetRecipe(in_recipe_));
    core_.Push(mat);
  }
  for (int i = 0; i < ics_.n_storage; ++i) {
    mat =
        Material::Create(this, batch_size(), context()->GetRecipe(out_recipe_));
    storage_.Push(mat);
  }

  LOG(cyclus::LEV_DEBUG2, "BReact") << "Batch Reactor entering the simuluation";
  LOG(cyclus::LEV_DEBUG2, "BReact") << str();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::HandleTick(int time) {
  LOG(cyclus::LEV_INFO3, "BReact") << name() << " is ticking at time "
                                   << time << " {";
  LOG(cyclus::LEV_DEBUG3, "BReact") << "The current phase is: "
                                    << phase_names_[phase_];

  switch (phase()) {
    case PROCESS:
      if (time == end_time()) {
        for (int i = 0; i < n_load(); i++) {
          MoveBatchOut_();
        }
        phase(WAITING);
      }
      break;

    case WAITING:
      if (n_core() == n_batches() &&
          end_time() + refuel_time() <= context()->time()) {
        phase(PROCESS);
      } 
      break;
      
    case INITIAL:
      // special case for a core primed to go
      if (n_core() == n_batches()) phase(PROCESS);
      break;
  }
  
  LOG(cyclus::LEV_INFO3, "BReact") << "}";
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::HandleTock(int time) {
  LOG(cyclus::LEV_INFO3, "BReact") << name() << " is tocking {";
  LOG(cyclus::LEV_DEBUG3, "BReact") << "The current phase is: "
                                    << phase_names_[phase_];
  switch (phase()) {
    case INITIAL: // falling through
    case WAITING:
      Refuel_();
      break;
  }
  LOG(cyclus::LEV_INFO3, "BReact") << "}";
}


//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<cyclus::RequestPortfolio<cyclus::Material>::Ptr>
BatchReactor::GetMatlRequests() {
  using cyclus::RequestPortfolio;
  using cyclus::Material;
  
  std::set<RequestPortfolio<Material>::Ptr> set;
  double order_size;

  switch (phase()) {
    // the initial phase requests as much fuel as necessary to achieve an entire
    // core
    case INITIAL:
      order_size = n_batches() * batch_size()
                   - core_.quantity() - reserves_.quantity()
                   - spillover_->quantity();
      if (preorder_time() == 0) order_size += batch_size() * n_reserves();
      if (order_size > 0) {
        RequestPortfolio<Material>::Ptr p = GetOrder_(order_size);
        set.insert(p);
      }
      break;

    // the default case is to request the reserve amount if the order time has
    // been reached
    default:
      order_size = n_reserves() * batch_size()
                   - reserves_.quantity() - spillover_->quantity();
      if (order_time() <= context()->time() &&
          order_size > 0) {
        RequestPortfolio<Material>::Ptr p = GetOrder_(order_size);
        set.insert(p);
      }
      break;
  }

  return set;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::AcceptMatlTrades(
    const std::vector< std::pair<cyclus::Trade<cyclus::Material>,
                                 cyclus::Material::Ptr> >& responses) {
  cyclus::Material::Ptr mat = responses.at(0).second;
  for (int i = 1; i < responses.size(); i++) {
    mat->Absorb(responses.at(i).second);
  }
  AddBatches_(mat);
}
  
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<cyclus::BidPortfolio<cyclus::Material>::Ptr>
BatchReactor::GetMatlBids(const cyclus::CommodMap<cyclus::Material>::type&
                          commod_requests) {
  using cyclus::Bid;
  using cyclus::BidPortfolio;
  using cyclus::CapacityConstraint;
  using cyclus::Converter;
  using cyclus::Material;
  using cyclus::Request;

  std::set<BidPortfolio<Material>::Ptr> ports;

  if (commod_requests.count(out_commodity_) > 0 && storage_.quantity() > 0) {
    const std::vector<Request<Material>::Ptr>& requests =
        commod_requests.at(out_commodity_);

    BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());

    std::vector<Request<Material>::Ptr>::const_iterator it;
    for (it = requests.begin(); it != requests.end(); ++it) {
      const Request<Material>::Ptr req = *it;
      double qty = std::min(req->target()->quantity(), storage_.quantity());
      Material::Ptr offer =
          Material::CreateUntracked(qty, context()->GetRecipe(out_recipe_));
      port->AddBid(req, offer, this);
    }

    CapacityConstraint<Material> cc(storage_.quantity());
    port->AddConstraint(cc);
    ports.insert(port);
  }
  return ports;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::GetMatlTrades(
    const std::vector< cyclus::Trade<cyclus::Material> >& trades,
    std::vector<std::pair<cyclus::Trade<cyclus::Material>,
                          cyclus::Material::Ptr> >& responses) {
  using cyclus::Material;
  using cyclus::Trade;
  using cyclus::ResCast;

  std::vector< cyclus::Trade<cyclus::Material> >::const_iterator it;
  for (it = trades.begin(); it != trades.end(); ++it) {
    LOG(cyclus::LEV_INFO5, "BReact") << name() << " just received an order.";

    double qty = it->amt;
    std::vector<Material::Ptr> manifest;
    try {
      // pop amount from inventory and blob it into one material
      manifest = ResCast<Material>(storage_.PopQty(qty));  
    } catch(cyclus::Error e) {
      std::string msg("BatchReactor experience an error: ");
      msg += e.what();
      throw cyclus::Error(msg);
    }
    Material::Ptr response = manifest[0];
    for (int i = 1; i < manifest.size(); i++) {
      response->Absorb(manifest[i]);
    }

    responses.push_back(std::make_pair(*it, response));
    LOG(cyclus::LEV_INFO5, "BatchReactor") << name()
                                           << " just received an order"
                                           << " for " << qty
                                           << " of " << out_commodity_;
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::phase(BatchReactor::Phase p) {
  LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name()
                                    << " is changing phases -";
  LOG(cyclus::LEV_DEBUG2, "BReact") << "  * from phase: " << phase_names_[phase_];
  LOG(cyclus::LEV_DEBUG2, "BReact") << "  * to phase: " << phase_names_[p];
  
  switch (p) {
    case PROCESS:
      start_time(context()->time());
  }
  phase_ = p;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Refuel_() {
  while(n_core() < n_batches() && reserves_.count() > 0) {
    MoveBatchIn_();
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::MoveBatchIn_() {
  LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name() << " added"
                                    <<  " a batch from its core.";
  try {
    core_.Push(reserves_.Pop());
  } catch(cyclus::Error e) {
      std::string msg("BatchReactor experience an error: ");
      msg += e.what();
      throw cyclus::Error(msg);
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::MoveBatchOut_() {
  using cyclus::Material;
  using cyclus::ResCast;
  
  LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name() << " removed"
                                    <<  " a batch from its core.";
  try {
    Material::Ptr mat = ResCast<Material>(core_.Pop());
    mat->Transmute(context()->GetRecipe(out_recipe()));
    storage_.Push(mat);
  } catch(cyclus::Error e) {
      std::string msg("BatchReactor experience an error: ");
      msg += e.what();
      throw cyclus::Error(msg);
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::RequestPortfolio<cyclus::Material>::Ptr
BatchReactor::GetOrder_(double size) {
  using cyclus::CapacityConstraint;
  using cyclus::Material;
  using cyclus::RequestPortfolio;
  using cyclus::Request;

  LOG(cyclus::LEV_DEBUG3, "BReact") << "BatchReactor " << name()
                                    << " is making an order of size: "
                                    << size;

  Material::Ptr mat =
      Material::CreateUntracked(size, context()->GetRecipe(in_recipe_));
  
  RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
  port->AddRequest(mat, this, in_commodity_);

  CapacityConstraint<Material> cc(size);
  port->AddConstraint(cc);

  return port;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::AddBatches_(cyclus::Material::Ptr mat) {
  using cyclus::Material;
  using cyclus::ResCast;

  LOG(cyclus::LEV_DEBUG3, "BReact") << "BatchReactor " << name()
                                    << " is adding " << mat->quantity()
                                    << " of material to its reserves.";

  spillover_->Absorb(mat);
  
  while (spillover_->quantity() >= batch_size()) {
    Material::Ptr batch = spillover_->ExtractQty(batch_size());
    reserves_.Push(batch);    
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::SetUpPhaseNames_() {
  phase_names_.insert(std::make_pair(INITIAL, "initialization"));
  phase_names_.insert(std::make_pair(PROCESS, "processing batch(es)"));
  phase_names_.insert(std::make_pair(WAITING, "waiting for fuel"));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
extern "C" cyclus::Model* ConstructBatchReactor(cyclus::Context* ctx) {
  return new BatchReactor(ctx);
}

} // namespace cycamore
