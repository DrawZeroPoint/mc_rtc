#pragma once

#include <mc_control/client_api.h>
#include <mc_control/mc_controller.h>

#include <mc_rtc/GUIState.h>

#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>
#include <nanomsg/reqrep.h>

#include <string>
#include <vector>

namespace mc_control
{

  /** Serves data and allow interaction with the controllers
   *
   * - Uses a PUB socket to send the data stream
   *
   * - Uses a PULL socket to handle requests
   */
  struct MC_CONTROL_DLLAPI ControllerServer
  {

    /** Constructor
     *
     * \param dt Controller timestep
     *
     * \param server_dt Publication timestep
     *
     * \param pub_bind_uri List of URI the PUB socket should bind to
     *
     * \param pull_bind_uri List of URI the PULL socket should bind to
     *
     * Check nanomsg documentation for supported protocols
     */
    ControllerServer(double dt, double server_dt,
                     const std::vector<std::string> & pub_bind_uri,
                     const std::vector<std::string> & pull_bind_uri);

    ~ControllerServer();

    /** Handle requests made by the GUI users */
    void handle_requests(mc_rtc::gui::StateBuilder & gui_builder);

    /** Publish the current GUI state */
    void publish(mc_rtc::gui::StateBuilder & gui_builder);

  private:
    unsigned int iter;
    unsigned int rate;

    int pub_socket_;
    int pull_socket_;
  };


}
