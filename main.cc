// --------------------------------------------------------------------------
//  _____                    ________   __
// |  __ \                   |  ___\ \ / /
// | |  \/_ __ ___  ___ _ __ | |_   \ V /          Open Source Tools for
// | | __| '__/ _ \/ _ \ '_ \|  _|  /   \            Automated Algorithmic
// | |_\ \ | |  __/  __/ | | | |   / /^\ \             Currency Trading
//  \____/_|  \___|\___|_| |_\_|   \/   \/
//
// --------------------------------------------------------------------------

// Copyright (C) 2014, 2106  Anthony Green <anthony@atgreen.org>
// Distrubuted under the terms of the GPL v3 or later.

// This progam pulls ticks from the A-MQ message bus and records them
// to disk.

#include <cstdlib>
#include <memory>
#include <unordered_map>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include <decaf/lang/Thread.h>
#include <decaf/lang/Runnable.h>

#include <activemq/library/ActiveMQCPP.h>
#include <activemq/core/ActiveMQConnectionFactory.h>
#include <cms/ExceptionListener.h>
#include <cms/MessageListener.h>

#include <json/json.h>

using namespace decaf::util::concurrent;
using namespace decaf::util;
using namespace decaf::lang;
using namespace activemq;
using namespace cms;
using namespace std;

#include "config.h"
 
class TickListener : public ExceptionListener,
		     public MessageListener,
  		     public Runnable {

private:
  Session *session;
  Connection *connection;
  Destination *destination;
  MessageConsumer *consumer;

  string brokerURI;

  unordered_map <string, FILE *> filemap;

public:
  TickListener () :
    brokerURI("tcp://broker-amq-tcp:61616?wireFormat=openwire") {
  }

  virtual void run () {
    try {

      // Create a ConnectionFactory
      std::auto_ptr<ConnectionFactory> 
	connectionFactory(ConnectionFactory::createCMSConnectionFactory(brokerURI));
      
      // Create a Connection
      connection = connectionFactory->createConnection("user", "password");
      connection->start();
      connection->setExceptionListener(this);
      
      session = connection->createSession(Session::AUTO_ACKNOWLEDGE);
      destination = session->createTopic("OANDA.TICK");
      
      consumer = session->createConsumer(destination);
      consumer->setMessageListener(this);

      std::cout << "Listening..." << std::endl;

      // Sleep forever
      while (true)
	sleep(1000);
      
    } catch (CMSException& ex) {
      
      printf (ex.getStackTraceString().c_str());
      exit (1);
      
    }
  }

  virtual void onMessage(const Message *msg)
  {
    json_object *jobj = json_tokener_parse (dynamic_cast<const TextMessage*>(msg)->getText().c_str());
    json_object *tick;

    if (json_object_object_get_ex (jobj, "tick", &jobj))
      {
	json_object *bid, *ask, *instrument, *ttime;
	if (json_object_object_get_ex (jobj, "bid", &bid) &&
	    json_object_object_get_ex (jobj, "ask", &ask) &&
	    json_object_object_get_ex (jobj, "instrument", &instrument) &&
	    json_object_object_get_ex (jobj, "time", &ttime))
	  {
	    string instrument_s = json_object_get_string (instrument);
	    FILE *f = filemap[instrument_s];
	    if (! f)
	      {
		char buf[512];
		struct tm *utc;
		time_t t;
		t = time (NULL);
		utc = gmtime (&t);
		strftime (buf, 512, "%Y%m%d-%H%M%S", utc);
		string fname = 
		  "/var/lib/greenfx/tickq-to-archive/" + instrument_s + "-" + buf + ".csv";
		f = filemap[instrument_s] = fopen (fname.c_str(), "a+");
		if (! f)
		  {
		    fprintf (stderr, "Error opening %s: %s\n",
			     fname.c_str(),
			     strerror (errno));
		    exit (1);
		  }
	      }		
	    
	    fprintf (f, "%s,%s,%s\n", 
		     json_object_get_string (ttime),
		     json_object_get_string (bid),
		     json_object_get_string (ask));
	    json_object_put (bid);
	    json_object_put (ask);
	    json_object_put (instrument);
	    json_object_put (ttime);
	  }
	else
	  {
	    // We are also leaking memory here - but this should never happen.
	    std::cerr << "ERROR: unrecognized json: " 
		      << dynamic_cast<const TextMessage*>(msg)->getText() << std::endl;
	  }
	json_object_put (tick);
      }

    json_object_put (jobj);
  }

  virtual void onException(const CMSException& ex)
  {
    printf (ex.getStackTraceString().c_str());
    exit (1);
  }
};

int main()
{
  std::cout << "tickq-to-archive, Copyright (C) 2014, 2016  Anthony Green" << std::endl;
  std::cout << "Program started by User " << getuid() << std::endl;

  activemq::library::ActiveMQCPP::initializeLibrary();

  TickListener tick_listener;
  Thread listener_thread(&tick_listener);
  listener_thread.start();
  listener_thread.join();

  std::cout << "Program ended." << std::endl;

  return 0;
}

