// Calimero KNX Data Secure peer for the kmx-aio interoperability test.
//
// It joins the KNX IP Secure routing group on the loopback interface as 1.1.1, keyed from an ETS keyring - read, verified
// and decrypted by Calimero's own keyring code, not by the library under test - and applies KNX Data Secure through
// Calimero's SecureApplicationLayer, with the group keys and senders the same keyring holds. It waits for the in-tree
// router's secured switch-on to 1/1/1, opened by Calimero, and answers with a secured switch-off of its own. It exits 0
// once it has received and answered, and 2 when the telegram does not arrive in time.
//
// Started by script/feature/knx/interop/run-data-secure-interop.sh as a single-file program:
//     java -cp calimero-core-3.0-M2.jar CalimeroDataSecurePeer.java --keyring <path> --password <pwd> --timeout <s>

import io.calimero.FrameEvent;
import io.calimero.GroupAddress;
import io.calimero.IndividualAddress;
import io.calimero.Priority;
import io.calimero.SerialNumber;
import io.calimero.cemi.CEMILData;
import io.calimero.link.KNXNetworkLink;
import io.calimero.link.KNXNetworkLinkIP;
import io.calimero.link.NetworkLinkListener;
import io.calimero.link.medium.TPSettings;
import io.calimero.secure.Keyring;
import io.calimero.secure.SecureApplicationLayer;
import io.calimero.secure.Security;

import java.net.NetworkInterface;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class CalimeroDataSecurePeer
{
    private static final IndividualAddress self = new IndividualAddress(1, 1, 1);
    private static final GroupAddress group = new GroupAddress(1, 1, 1);
    // TPCI 00, then A_GroupValue_Write with the switch value in the APCI's low bits.
    private static final byte[] switchOn = {0x00, (byte) 0x81};
    private static final byte[] switchOff = {0x00, (byte) 0x80};
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
        final byte[] backboneKey = keyring.decryptKey(backbone.groupKey().orElseThrow(), password);
        final Security security = Security.newSecurity();
        security.useKeyring(keyring, password);
        final NetworkInterface loopback = NetworkInterface.getByName(options.getOrDefault("--interface", "lo"));

        final CountDownLatch received = new CountDownLatch(1);
        // A secure application layer whose sequence number is still zero takes itself to be unsynchronised and secures under
        // tool access, which a group telegram cannot use. Start it where ETS and xknx start one: milliseconds since
        // 2018-01-05T00:00:00Z. The serial number is `kmx` and a five.
        final var sequenceNumbers = new SecureApplicationLayer.SequenceNumbers(System.currentTimeMillis() - 1_515_110_400_000L, 0L,
                new HashMap<>(), new HashMap<>());
        try (KNXNetworkLink link = KNXNetworkLinkIP.newSecureRoutingLink(loopback, backbone.multicastGroup(), backboneKey,
                backbone.latencyTolerance(), new TPSettings(self));
             SecureApplicationLayer sal = new SecureApplicationLayer(link, security, SerialNumber.of(0x6B6D7805L), sequenceNumbers))
        {
            sal.addListener(new NetworkLinkListener()
            {
                @Override
                public void indication(final FrameEvent event)
                {
                    if (event.getFrame() instanceof final CEMILData frame && group.equals(frame.getDestination())
                            && Arrays.equals(frame.getPayload(), switchOn))
                    {
                        System.out.println("received from the in-tree router, opened by Data Secure: " + frame + ", TPDU "
                                + HexFormat.of().formatHex(frame.getPayload()));
                        received.countDown();
                    }
                }
            });
            System.out.println("Calimero Data Secure over secure routing on " + backbone.multicastGroup().getHostAddress() + " via "
                    + loopback.getName() + " as " + self + ", group keys for " + security.groupKeys().keySet());

            if (!received.await(Long.parseLong(options.getOrDefault("--timeout", "40")), TimeUnit.SECONDS))
                exit(2, "no secured switch-on to " + group + " in time");
            for (int answer = 0; answer < answers; ++answer)
            {
                final Optional<byte[]> secured = sal.secureGroupObject(self, group, switchOff);
                if (secured.isEmpty())
                    exit(3, "Calimero has no key to secure " + group + " with");
                link.sendRequest(group, Priority.LOW, secured.get());
                Thread.sleep(answerIntervalMs);
            }
            System.out.println("answered " + answers + " times to " + group + " under Data Secure");
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
