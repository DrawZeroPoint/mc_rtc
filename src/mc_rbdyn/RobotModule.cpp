/*
 * Copyright 2015-2019 CNRS-UM LIRMM, CNRS-AIST JRL
 */

#include <mc_rbdyn/RobotModule.h>

namespace mc_rbdyn
{

// Repeat static constexpr declarations
// See also https://stackoverflow.com/q/8016780
constexpr double RobotModule::Gripper::Safety::DEFAULT_PERCENT_VMAX;
constexpr double RobotModule::Gripper::Safety::DEFAULT_ACTUAL_COMMAND_DIFF_TRIGGER;
constexpr double RobotModule::Gripper::Safety::DEFAULT_RELEASE_OFFSET;
constexpr unsigned int RobotModule::Gripper::Safety::DEFAULT_OVER_COMMAND_LIMIT_ITER_N;

DevicePtrVector::DevicePtrVector(const DevicePtrVector & v) : std::vector<DevicePtr>()
{
  reserve(v.size());
  for(const auto & s : v)
  {
    push_back(s->clone());
  }
}

DevicePtrVector & DevicePtrVector::operator=(const DevicePtrVector & v)
{
  if(&v == this)
  {
    return *this;
  }
  resize(v.size());
  for(size_t i = 0; i < v.size(); ++i)
  {
    (*this)[i] = v[i]->clone();
  }
  return *this;
}

VisualMap::VisualMap(const std::map<std::string, std::vector<mc_rbdyn_urdf::Visual>> & rhs)
{
  *this = rhs;
}

VisualMap & VisualMap::operator=(const std::map<std::string, std::vector<mc_rbdyn_urdf::Visual>> & rhs)
{
  auto box = [](const mc_rbdyn_urdf::Geometry::Box & box) {
    rbd::parsers::Geometry::Box ret;
    ret.size.setConstant(box.size);
    return ret;
  };
  auto cylinder = [](const mc_rbdyn_urdf::Geometry::Cylinder & c) {
    rbd::parsers::Geometry::Cylinder ret;
    ret.radius = c.radius;
    ret.length = c.length;
    return ret;
  };
  auto sphere = [](const mc_rbdyn_urdf::Geometry::Sphere & sphere) {
    rbd::parsers::Geometry::Sphere ret;
    ret.radius = sphere.radius;
    return ret;
  };
  auto mesh = [](const mc_rbdyn_urdf::Geometry::Mesh & mesh) {
    rbd::parsers::Geometry::Mesh ret;
    ret.filename = mesh.filename;
    ret.scale = mesh.scale;
    return ret;
  };
  auto convertGeometry = [&box, &cylinder, &sphere, &mesh](const mc_rbdyn_urdf::Geometry & geom) {
    rbd::parsers::Geometry ret;
    switch(geom.type)
    {
      case mc_rbdyn_urdf::Geometry::BOX:
        ret.type = rbd::parsers::Geometry::BOX;
        ret.data = box(boost::get<mc_rbdyn_urdf::Geometry::Box>(geom.data));
        break;
      case mc_rbdyn_urdf::Geometry::CYLINDER:
        ret.type = rbd::parsers::Geometry::CYLINDER;
        ret.data = cylinder(boost::get<mc_rbdyn_urdf::Geometry::Cylinder>(geom.data));
        break;
      case mc_rbdyn_urdf::Geometry::SPHERE:
        ret.type = rbd::parsers::Geometry::SPHERE;
        ret.data = sphere(boost::get<mc_rbdyn_urdf::Geometry::Sphere>(geom.data));
        break;
      case mc_rbdyn_urdf::Geometry::MESH:
        ret.type = rbd::parsers::Geometry::MESH;
        ret.data = mesh(boost::get<mc_rbdyn_urdf::Geometry::Mesh>(geom.data));
        break;
      default:
        break;
    };
    return ret;
  };
  auto convertVisual = [&convertGeometry](const mc_rbdyn_urdf::Visual & v) {
    rbd::parsers::Visual ret;
    ret.name = v.name;
    ret.origin = v.origin;
    ret.geometry = convertGeometry(v.geometry);
    return ret;
  };
  this->clear();
  for(const auto & p : rhs)
  {
    std::transform(p.second.begin(), p.second.end(), std::back_inserter((*this)[p.first]), convertVisual);
  }
  return *this;
}

static inline rbd::parsers::Limits from_mc_rbdyn_urdf(const mc_rbdyn_urdf::Limits & limits)
{
  rbd::parsers::Limits l;
  l.lower = limits.lower;
  l.upper = limits.upper;
  l.velocity = limits.velocity;
  l.torque = limits.torque;
  return l;
}

RobotModule::RobotModule(const std::string & name, const rbd::parsers::ParserResult & res)
: RobotModule("/CREATED/BY/MC/RTC/", name)
{
  rsdf_dir = "";
  init(res);
}

RobotModule::RobotModule(const std::string & name, const mc_rbdyn_urdf::URDFParserResult & res)
: RobotModule("/CREATED/BY/MC/RTC/", name)
{
  mc_rtc::log::warning("This function is deprecated, use rbd::parsers instead of mc_rbdyn_urdf");
  mb = res.mb;
  mbc = res.mbc;
  mbg = res.mbg;
  _collisionTransforms = res.collision_tf;
  boundsFromURDF(res.limits);
  _visual = res.visual;
  make_default_ref_joint_order();
  expand_stance();
}

void RobotModule::init(const rbd::parsers::ParserResult & res)
{
  mb = res.mb;
  mbc = res.mbc;
  mbg = res.mbg;
  for(const auto & col : res.collision)
  {
    const auto & body = col.first;
    const auto & cols = col.second;
    if(cols.size())
    {
      _collisionTransforms[body] = cols[0].origin;
    }
  }
  boundsFromURDF(res.limits);
  _visual = res.visual;
  make_default_ref_joint_order();
  expand_stance();
}

RobotModule::Gripper::Gripper(const std::string & name, const std::vector<std::string> & joints, bool reverse_limits)
: Gripper(name, joints, reverse_limits, nullptr, nullptr)
{
}

RobotModule::Gripper::Gripper(const std::string & name,
                              const std::vector<std::string> & joints,
                              bool reverse_limits,
                              const Safety & safety)
: Gripper(name, joints, reverse_limits, &safety, nullptr)
{
}

RobotModule::Gripper::Gripper(const std::string & name,
                              const std::vector<std::string> & joints,
                              bool reverse_limits,
                              const Safety & safety,
                              const std::vector<Mimic> & mimics)
: Gripper(name, joints, reverse_limits, &safety, &mimics)
{
}

RobotModule::Gripper::Gripper(const std::string & name,
                              const std::vector<std::string> & joints,
                              bool reverse_limits,
                              const Safety * safety,
                              const std::vector<Mimic> * mimics)
: name(name), joints(joints), reverse_limits(reverse_limits), hasSafety_(safety != nullptr),
  hasMimics_(mimics != nullptr)
{
  if(mimics)
  {
    mimics_ = *mimics;
  }
  if(safety)
  {
    safety_ = *safety;
  }
}

void RobotModule::boundsFromURDF(const rbd::parsers::Limits & limits)
{
  auto neg_bound = [](const std::map<std::string, std::vector<double>> & v) {
    std::map<std::string, std::vector<double>> res;
    for(const auto & vi : v)
    {
      res[vi.first] = vi.second;
      for(auto & vj : res[vi.first])
      {
        vj = -vj;
      }
    }
    return res;
  };
  _bounds = {
      limits.lower, limits.upper, neg_bound(limits.velocity), limits.velocity, neg_bound(limits.torque), limits.torque,
  };
}

void RobotModule::boundsFromURDF(const mc_rbdyn_urdf::Limits & limits)
{
  mc_rtc::log::warning("This function is deprecated, use rbd::parsers instead of mc_rbdyn_urdf");
  boundsFromURDF(from_mc_rbdyn_urdf(limits));
}

void RobotModule::expand_stance()
{
  for(const auto & j : mb.joints())
  {
    if(!_stance.count(j.name()))
    {
      _stance[j.name()] = j.zeroParam();
    }
  }
}

void RobotModule::make_default_ref_joint_order()
{
  _ref_joint_order.resize(0);
  for(const auto & j : mb.joints())
  {
    if(j.dof() >= 1 && j.type() != rbd::Joint::Free)
    {
      _ref_joint_order.push_back(j.name());
    }
  }
}

RobotModule::bounds_t urdf_limits_to_bounds(const rbd::parsers::Limits & limits)
{
  RobotModule::bounds_t ret = {};
  ret.reserve(6);
  ret.push_back(limits.lower);
  ret.push_back(limits.upper);
  auto convert = [](const std::map<std::string, std::vector<double>> & l) {
    auto ret = l;
    for(auto & el : ret)
    {
      for(auto & e : el.second)
      {
        e = -e;
      }
    }
    return ret;
  };
  ret.push_back(convert(limits.velocity));
  ret.push_back(limits.velocity);
  ret.push_back(convert(limits.torque));
  ret.push_back(limits.torque);
  return ret;
}

RobotModule::bounds_t urdf_limits_to_bounds(const mc_rbdyn_urdf::Limits & limits)
{
  mc_rtc::log::warning("This function is deprecated, use rbd::parsers instead of mc_rbdyn_urdf");
  return urdf_limits_to_bounds(from_mc_rbdyn_urdf(limits));
}

} // namespace mc_rbdyn
