#include <mc_control/Configuration.h>

#include <mc_rtc/logging.h>

#include <fstream>

namespace mc_control
{

namespace
{
  Configuration::Entry getEntry(const Json::Value & v, const std::string & key)
  {
    if(v.isMember(key))
    {
      return Configuration::Entry(v[key]);
    }
    throw Configuration::Exception("No entry named " + key + " in the configuration");
  }
}

Configuration::Exception::Exception(const std::string & msg)
: msg(msg)
{
}

const char * Configuration::Exception::what() const noexcept
{
  return msg.c_str();
}

Configuration::Entry::Entry(const Json::Value & v)
: v(v)
{
}

bool Configuration::Entry::isMember(const std::string & key) const
{
  return v.isMember(key);
}

bool Configuration::isMember(const std::string & key) const
{
  return v.isMember(key);
}

Configuration::Entry Configuration::Entry::operator()(const std::string & key) const
{
  return getEntry(v, key);
}

Configuration::Entry::operator bool() const
{
  if(v.isBool() || v.isInt())
  {
    return v.asBool();
  }
  throw Configuration::Exception("Stored Json value is not a bool");
}

Configuration::Entry::operator int() const
{
  if(v.isInt())
  {
    return v.asInt();
  }
  throw Configuration::Exception("Stored Json value is not an int");
}

Configuration::Entry::operator unsigned int() const
{
  if(v.isUInt() || (v.isInt() && v.asInt() >= 0))
  {
    return v.asUInt();
  }
  throw Configuration::Exception("Stored Json value is not an unsigned int");
}

Configuration::Entry::operator double() const
{
  if(v.isNumeric())
  {
    return v.asDouble();
  }
  throw Configuration::Exception("Stored Json value is not a double");
}

Configuration::Entry::operator std::string() const
{
  if(v.isString())
  {
    return v.asString();
  }
  throw Configuration::Exception("Stored Json value is not a string");
}

Configuration::Entry::operator Eigen::Vector3d() const
{
  if(v.isArray() && v.size() == 3 && v[0].isNumeric())
  {
    Eigen::Vector3d ret;
    ret << v[0].asDouble(), v[1].asDouble(), v[2].asDouble();
    return ret;
  }
  throw Configuration::Exception("Stored Json value is not a Vector3d");
}

Configuration::Entry::operator Eigen::Vector6d() const
{
  if(v.isArray() && v.size() == 6 && v[0].isNumeric())
  {
    Eigen::Vector6d ret;
    ret << v[0].asDouble(), v[1].asDouble(), v[2].asDouble(),
           v[3].asDouble(), v[4].asDouble(), v[5].asDouble();
    return ret;
  }
  throw Configuration::Exception("Stored Json value is not a Vector6d");
}

Configuration::Entry::operator Eigen::VectorXd() const
{
  if(v.isArray() && (v.size() == 0 || v[0].isNumeric()))
  {
    Eigen::VectorXd ret(v.size());
    for(size_t i = 0; i < v.size(); ++i)
    {
      ret(i) = v[static_cast<int>(i)].asDouble();
    }
    return ret;
  }
  throw Configuration::Exception("Stored Json value is not a Vector6d");
}

Configuration::Entry::operator Eigen::Quaterniond() const
{
  if(v.isArray() && v.size() == 4 && v[0].isNumeric())
  {
    return Eigen::Quaterniond(v[0].asDouble(), v[1].asDouble(),
                              v[2].asDouble(), v[3].asDouble())
                             .normalized();
  }
  throw Configuration::Exception("Stored Json value is not a Quaterniond");
}

Configuration::Configuration(const std::string & path)
: v()
{
  load(path);
}

void Configuration::load(const std::string & path)
{
  std::ifstream ifs(path);
  if(ifs.bad())
  {
    LOG_ERROR("Failed to open controller configuration file: " << path)
  }
  Json::Value newV;
  try
  {
    ifs >> newV;
  }
  catch(const std::runtime_error & exc)
  {
    LOG_ERROR("Failed to read configuration file: " << path)
    LOG_WARNING(exc.what())
  }
  for(const auto & m : newV.getMemberNames())
  {
    v[m] = newV[m];
  }
}

Configuration::Entry Configuration::operator()(const std::string & key) const
{
  return getEntry(v, key);
}

template<>
void Configuration::operator()(const std::string & key, std::string & v) const
{
  try
  {
    v = (std::string)(*this)(key);
  }
  catch(Exception &)
  {
  }
}

template<>
void Configuration::Entry::operator()(const std::string & key, std::string & v) const
{
  try
  {
    v = (std::string)(*this)(key);
  }
  catch(Exception &)
  {
  }
}

}