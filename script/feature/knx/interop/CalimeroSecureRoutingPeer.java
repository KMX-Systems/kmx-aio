// Calimero KNX IP Secure routing peer for the kmx-aio interoperability test.
//
// It joins the routing group on the loopback interface with the backbone key of an ETS keyring - read and decrypted
// by Calimero's own keyring code, not by the library under test - waits for the in-tree router's switch-on to 1/2/3,
// and answers with a switch-on of its own to 1/2/4. It exits 0 once it has received and answered, and 2 when the
// telegram does not arrive in time.
//
// Started by script/feature/knx/interop/run-secure-routing-interop.sh as a single-file program:
//     java -cp calimero-core-3.0-M2.jar CalimeroSecureRoutingPeer.java --keyring <path> --password <pwd> --timeout <s>

import io.calimero.FrameEvent;
import io.calimero.GroupAddress;
import io.calimero.IndividualAddress;
import io.calimero.Priority;
import io.calimero.cemi.CEMILData;
import io.calimero.link.KNXNetworkLink;
import io.calimero.link.KNXNetworkLinkIP;
import io.calimero.link.NetworkLinkListener;
import io.calimero.link.medium.TPSettings;
import io.calimero.secure.Keyring;

import java.net.NetworkInterface;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class CalimeroSecureRoutingPeer
{
    private static final GroupAddress expectedGroup = new GroupAddress(1, 2, 3);
    private static final GroupAddress answerGroup = new GroupAddress(1, 2, 4);
    // Answers are repeated for a few seconds, so that a router whose timer is still converging gets more than one chance.
    private static final int answers = 10;
    private static final long answerIntervalMs = 500;

    public static void main(final String[] arguments) throws Exception
    {
        final Map<String, String> options = options(arguments);
        final char[] password = options.get("--password").toCharArray();
        final Keyring keyring = Keyring.load(options.get("--keyring"));
        if (!keyring.verifySignature(password))
            exit(1, "the keyring signature does not verify");
        final Keyring.Backbone backbone = keyring.backbone().orElseThrow();
        final byte[] key = keyring.decryptKey(backbone.groupKey().orElseThrow(), password);
        final NetworkInterface loopback = NetworkInterface.getByName(options.getOrDefault("--interface", "lo"));

        final CountDownLatch received = new CountDownLatch(1);
        try (KNXNetworkLink link = KNXNetworkLinkIP.newSecureRoutingLink(loopback, backbone.multicastGroup(), key,
                backbone.latencyTolerance(), new TPSettings(new IndividualAddress(1, 1, 251))))
        {
            link.addLinkListener(new NetworkLinkListener()
            {
                @Override
                public void indication(final FrameEvent event)
                {
                    if (event.getFrame() instanceof final CEMILData frame && expectedGroup.equals(frame.getDestination()))
                    {
                        System.out.println("received from the in-tree router: " + frame + ", TPDU "
                                + HexFormat.of().formatHex(frame.getPayload()));
                        received.countDown();
                    }
                }
            });
            System.out.println("Calimero secure routing link on " + backbone.multicastGroup().getHostAddress() + " via "
                    + loopback.getName() + ", latency tolerance " + backbone.latencyTolerance());

            if (!received.await(Long.parseLong(options.getOrDefault("--timeout", "40")), TimeUnit.SECONDS))
                exit(2, "no switch-on to " + expectedGroup + " in time");
            for (int answer = 0; answer < answers; ++answer)
            {
                // TPCI 00, then A_GroupValue_Write with the value 1 in the APCI's low bits.
                link.sendRequest(answerGroup, Priority.LOW, (byte) 0x00, (byte) 0x81);
                Thread.sleep(answerIntervalMs);
            }
            System.out.println("answered " + answers + " times to " + answerGroup);
        }
    }

    private static Map<String, String> options(final String[] arguments)
    {
        final Map<String, String> result = new HashMap<>();
        for (int index = 0; (index + 1) < arguments.length; index += 2)
            result.put(arguments[index], arguments[index + 1]);
        return result;
    }

    private static void exit(final int status, final String reason)
    {
        System.out.println(reason);
        System.exit(status);
    }
}
